#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>

/* --------------------- Config --------------------- */
#define MAX_LINE   256
#define MAX_ARGS   64
#define MAX_CMDS   8      /* max stages in pipeline */
#define MAX_JOBS   64

/* --------------------- I/O helpers --------------------- */
static void putstr(const char *s) { ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r; }
static void puterr(const char *s) { ssize_t r = write(STDERR_FILENO, s, strlen(s)); (void)r; }

/* write unsigned int without stdio */
static void write_uint(int fd, int v){
    char buf[16]; int i=0;
    if (v==0){ (void)write(fd,"0",1); return; }
    char rev[16]; int r=0;
    while (v>0 && r<(int)sizeof(rev)) { rev[r++] = (char)('0' + (v%10)); v/=10; }
    while (r--) buf[i++] = rev[r];
    (void)write(fd, buf, i);
}

/* --------------------- Job control --------------------- */
typedef enum { JOB_RUNNING=0, JOB_STOPPED=1, JOB_DONE=2 } job_state_t;
typedef struct {
    int   used;
    int   id;              /* 1-based */
    pid_t pgid;            /* process group id */
    int   background;      /* 1 = bg, 0 = fg */
    job_state_t state;
    char  cmdline[MAX_LINE];
} job_t;

static job_t jobs[MAX_JOBS];
static pid_t shell_pgid = 0;
static struct termios shell_tmodes;

/* --------------------- Prototypes --------------------- */
static void install_shell(void);
static void give_terminal_to(pid_t pgid);
static void ignore_job_signals_in_shell(void);
static void reap_done_jobs(void);

static void trim_trailing(char *s);
static void skip_ws(char **p);
static int  parse_argv(char *s, char **argv);
static int  split_pipeline(char *line, char *stages[], int max);

static void s_ncpy(char *dst, const char *src, size_t n);

static void try_exec_with_path(char **argv);
static void exec_simple(char **argv);
static void apply_redirs(char **argv);

static int  add_job(pid_t pgid, int bg, const char *cmdline);
static job_t* find_job_by_pgid(pid_t pgid);
static job_t* find_job_by_id(int id);
static void remove_job(job_t *j);
static void print_job(const job_t *j);
static void mark_job_state_from_child(pid_t pid, int status);

static int  read_line(char *buf, int maxlen);

static int  try_builtins(char **argv);
static int  builtin_cd(char **argv);
static void builtin_jobs(void);
static void resume_job_bg(job_t *j);
static void resume_job_fg(job_t *j);

static pid_t launch_pipeline(char *stage_strs[], int nstages, char *cmdline, int background, pid_t *out_pgid);

/* --------------------- Tiny utils --------------------- */
static void trim_trailing(char *s){
    int n = (int)strlen(s);
    while (n>0 && (s[n-1]==' ' || s[n-1]=='\t')) { s[--n] = '\0'; }
}
static void skip_ws(char **p){ while (**p==' ' || **p=='\t') (*p)++; }

/* split into argv by spaces/tabs, no quotes */
static int parse_argv(char *s, char **argv){
    int argc=0; char *p=s;
    while (*p){
        skip_ws(&p);
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p!=' ' && *p!='\t') p++;
        if (*p){ *p='\0'; p++; }
        if (argc >= MAX_ARGS-1) break;
    }
    argv[argc] = NULL;
    return argc;
}

/* split pipeline into command substrings by '|' */
static int split_pipeline(char *line, char *stages[], int max){
    int n=0; char *p=line;
    while (*p){
        skip_ws(&p);
        if (!*p) break;
        stages[n++] = p;
        while (*p && *p!='|') p++;
        if (*p=='|'){ *p='\0'; p++; }
        if (n>=max) break;
    }
    return n ? n : 1;
}

/* safe copy */
static void s_ncpy(char *dst, const char *src, size_t n){
    size_t i=0; for (; i+1<n && src[i]; ++i) dst[i]=src[i];
    dst[i]='\0';
}

/* --------------------- PATH search (no execvp) --------------------- */
static void try_exec_with_path(char **argv){
    if (strchr(argv[0], '/')) {
        execv(argv[0], argv);
        _exit(127);
    }
    char *path = getenv("PATH");
    if (!path) _exit(127);

    char buf[512];
    char *p=path;
    while (*p){
        char *start=p;
        while (*p && *p!=':') p++;
        int len = (int)(p-start);
        if (len>0 && len < (int)sizeof(buf)-1){
            int off=0;
            for (int i=0;i<len && off<(int)sizeof(buf)-1;i++) buf[off++]=start[i];
            if (off<(int)sizeof(buf)-1) buf[off++]='/';
            const char *nm = argv[0];
            while (*nm && off<(int)sizeof(buf)-1) buf[off++]=*nm++;
            buf[off]='\0';
            execv(buf, argv); /* try candidate */
        }
        if (*p==':') p++;
    }
    _exit(127);
}

/* --------------------- Signals --------------------- */
static void sigchld_handler(int sig){
    (void)sig;
    int status; pid_t pid;
    /* Reap as many as available, including stopped/continued */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0){
        mark_job_state_from_child(pid, status);
    }
}

static void ignore_job_signals_in_shell(void){
    signal(SIGINT,  SIG_IGN);   /* ^C */
    signal(SIGTSTP, SIG_IGN);   /* ^Z */
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
}

/* --------------------- Jobs table --------------------- */
static int next_job_id(void){
    int maxid=0;
    for (int i=0;i<MAX_JOBS;i++) if (jobs[i].used && jobs[i].id>maxid) maxid=jobs[i].id;
    return maxid+1;
}
static int add_job(pid_t pgid, int bg, const char *cmdline){
    for (int i=0;i<MAX_JOBS;i++){
        if (!jobs[i].used){
            jobs[i].used=1;
            jobs[i].id = next_job_id();
            jobs[i].pgid=pgid;
            jobs[i].background=bg;
            jobs[i].state=JOB_RUNNING;
            s_ncpy(jobs[i].cmdline, cmdline, sizeof(jobs[i].cmdline));
            return jobs[i].id;
        }
    }
    return -1;
}
static job_t* find_job_by_pgid(pid_t pgid){
    for (int i=0;i<MAX_JOBS;i++) if (jobs[i].used && jobs[i].pgid==pgid) return &jobs[i];
    return NULL;
}
static job_t* find_job_by_id(int id){
    for (int i=0;i<MAX_JOBS;i++) if (jobs[i].used && jobs[i].id==id) return &jobs[i];
    return NULL;
}
static void remove_job(job_t *j){ if (j) memset(j,0,sizeof(*j)); }

static void print_job(const job_t *j){
    if (!j || !j->used) return;
    const char *st = (j->state==JOB_RUNNING? "Running" :
                      j->state==JOB_STOPPED? "Stopped" : "Done");
    (void)write(STDOUT_FILENO, "[", 1);
    write_uint(STDOUT_FILENO, j->id);
    (void)write(STDOUT_FILENO, "] ", 2);
    write_uint(STDOUT_FILENO, (int)j->pgid);
    (void)write(STDOUT_FILENO, " ", 1);
    (void)write(STDOUT_FILENO, st, strlen(st));
    (void)write(STDOUT_FILENO, ": ", 2);
    (void)write(STDOUT_FILENO, j->cmdline, strlen(j->cmdline));
    (void)write(STDOUT_FILENO, "\n", 1);
}

static void mark_job_state_from_child(pid_t pid, int status){
    pid_t pg = getpgid(pid);
    job_t *j = find_job_by_pgid(pg);
    if (!j) return;

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        j->state = JOB_DONE;
    } else if (WIFSTOPPED(status)) {
        j->state = JOB_STOPPED;
    } else if (WIFCONTINUED(status)) {
        j->state = JOB_RUNNING;
    }
}

static void reap_done_jobs(void){
    int status; pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0){
        mark_job_state_from_child(pid, status);
    }
    for (int i=0;i<MAX_JOBS;i++){
        if (jobs[i].used && jobs[i].state==JOB_DONE){
            remove_job(&jobs[i]);
        }
    }
}

/* --------------------- Terminal ownership --------------------- */
static void give_terminal_to(pid_t pgid){
    (void)tcsetpgrp(STDIN_FILENO, pgid);
}
static void install_shell(void){
    shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    give_terminal_to(shell_pgid);
    (void)tcgetattr(STDIN_FILENO, &shell_tmodes);
    ignore_job_signals_in_shell();
}

/* --------------------- Builtins --------------------- */
static int is_number(const char *s){
    if (!s || !*s) return 0;
    for (const char *p=s; *p; ++p) if (*p<'0'||*p>'9') return 0;
    return 1;
}

static int builtin_cd(char **argv){
    const char *dir = argv[1] ? argv[1] : getenv("HOME");
    if (!dir){ puterr("cd: HOME not set\n"); return -1; }
    if (chdir(dir) < 0){ puterr("cd: failed\n"); return -1; }
    return 0;
}

static void builtin_jobs(void){
    for (int i=0;i<MAX_JOBS;i++) if (jobs[i].used) print_job(&jobs[i]);
}

static void resume_job_bg(job_t *j){
    if (!j){ puterr("bg: no such job\n"); return; }
    j->background = 1;
    j->state = JOB_RUNNING;
    (void)kill(-j->pgid, SIGCONT);
}

static void resume_job_fg(job_t *j){
    if (!j){ puterr("fg: no such job\n"); return; }
    j->background = 0;
    j->state = JOB_RUNNING;
    give_terminal_to(j->pgid);
    (void)kill(-j->pgid, SIGCONT);

    int status; pid_t w;
    do {
        w = waitpid(-j->pgid, &status, WUNTRACED);
        if (w==-1 && errno==ECHILD) break;
    } while (!(WIFEXITED(status)||WIFSIGNALED(status)||WIFSTOPPED(status)));

    give_terminal_to(shell_pgid);
    if (WIFSTOPPED(status)) {
        job_t *jj = find_job_by_pgid(j->pgid);
        if (jj) jj->state = JOB_STOPPED;
    } else {
        remove_job(find_job_by_pgid(j->pgid));
    }
}

static int try_builtins(char **argv){
    if (!argv[0]) return 1;
    if (strcmp(argv[0],"exit")==0) _exit(0);
    if (strcmp(argv[0],"cd")==0)   { (void)builtin_cd(argv); return 1; }
    if (strcmp(argv[0],"jobs")==0) { builtin_jobs(); return 1; }
    if (strcmp(argv[0],"bg")==0) {
        job_t *j=NULL;
        if (argv[1] && argv[1][0]=='%' && is_number(argv[1]+1)) j = find_job_by_id(atoi(argv[1]+1));
        else if (argv[1] && is_number(argv[1])) j = find_job_by_id(atoi(argv[1]));
        else { for (int i=MAX_JOBS-1;i>=0;i--) if (jobs[i].used && jobs[i].state==JOB_STOPPED){ j=&jobs[i]; break; } }
        resume_job_bg(j);
        return 1;
    }
    if (strcmp(argv[0],"fg")==0) {
        job_t *j=NULL;
        if (argv[1] && argv[1][0]=='%' && is_number(argv[1]+1)) j = find_job_by_id(atoi(argv[1]+1));
        else if (argv[1] && is_number(argv[1])) j = find_job_by_id(atoi(argv[1]));
        else { for (int i=MAX_JOBS-1;i>=0;i--) if (jobs[i].used && jobs[i].state!=JOB_DONE){ j=&jobs[i]; break; } }
        resume_job_fg(j);
        return 1;
    }
    return 0;
}

/* --------------------- Redirection + exec --------------------- */
static void apply_redirs(char **argv){
    for (int i=0; argv[i]; ++i){
        if (strcmp(argv[i],">")==0 && argv[i+1]) {
            int fd = open(argv[i+1], O_WRONLY|O_CREAT|O_TRUNC, 0644);
            if (fd<0){ puterr("open > failed\n"); _exit(1); }
            (void)dup2(fd, STDOUT_FILENO); (void)close(fd);
            argv[i]=NULL; break;
        } else if (strcmp(argv[i],"<")==0 && argv[i+1]) {
            int fd = open(argv[i+1], O_RDONLY);
            if (fd<0){ puterr("open < failed\n"); _exit(1); }
            (void)dup2(fd, STDIN_FILENO); (void)close(fd);
            argv[i]=NULL; break;
        }
    }
}

static void exec_simple(char **argv){
    apply_redirs(argv);
    signal(SIGINT,  SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    try_exec_with_path(argv);
    puterr("command not found\n");
    _exit(127);
}

/* --------------------- Pipelines (n-stage) --------------------- */
static pid_t launch_pipeline(char *stage_strs[], int nstages, char *cmdline, int background, pid_t *out_pgid){
    int pipes[MAX_CMDS-1][2];
    for (int i=0;i<nstages-1;i++){
        if (pipe(pipes[i])<0){ puterr("pipe failed\n"); return -1; }
    }

    pid_t pgid = 0;
    for (int s=0; s<nstages; s++){
        char *argv[MAX_ARGS];
        parse_argv(stage_strs[s], argv);

        pid_t pid = fork();
        if (pid<0){ puterr("fork failed\n"); return -1; }

        if (pid==0){
            if (pgid==0) pgid=getpid();
            setpgid(getpid(), pgid);

            if (s>0)           (void)dup2(pipes[s-1][0], STDIN_FILENO);
            if (s<nstages-1)   (void)dup2(pipes[s][1],   STDOUT_FILENO);
            for (int i=0;i<nstages-1;i++){ (void)close(pipes[i][0]); (void)close(pipes[i][1]); }

            exec_simple(argv); /* never returns */
        } else {
            if (pgid==0) pgid=pid;
            setpgid(pid, pgid);
        }
    }

    for (int i=0;i<nstages-1;i++){ (void)close(pipes[i][0]); (void)close(pipes[i][1]); }

    if (!background){
        give_terminal_to(pgid);
        int status; pid_t w;
        do {
            w = waitpid(-pgid, &status, WUNTRACED);
            if (w==-1 && errno==ECHILD) break;
        } while (!(WIFEXITED(status)||WIFSIGNALED(status)||WIFSTOPPED(status)));
        give_terminal_to(shell_pgid);

        if (WIFSTOPPED(status)){
            (void)add_job(pgid, 0, cmdline);
            job_t *j = find_job_by_pgid(pgid);
            if (j) j->state = JOB_STOPPED;
        }
    } else {
        (void)add_job(pgid, 1, cmdline);
    }
    if (out_pgid) *out_pgid = pgid;
    return pgid;
}

/* --------------------- Read line (raw, system calls) --------------------- */
static int read_line(char *buf, int maxlen){
    int off=0;
    while (off < maxlen-1){
        char c; ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n<=0) break;
        if (c=='\n') break;
        buf[off++] = c;
    }
    buf[off]='\0';
    return off;
}

/* --------------------- Main loop --------------------- */
int main(void){
    install_shell();

    char line[MAX_LINE];
    while (1){
        reap_done_jobs(); /* periodic cleanup */
        putstr("mysh$ ");

        int n = read_line(line, sizeof(line));
        if (n<=0){ putstr("\n"); break; }

        trim_trailing(line);
        if (line[0]=='\0') continue;

        /* background? */
        int background = 0;
        int L = (int)strlen(line);
        if (L>0 && line[L-1]=='&'){
            background = 1;
            line[L-1]='\0';
            trim_trailing(line);
        }

        /* split pipeline */
        char *stages[MAX_CMDS];
        int nstages = split_pipeline(line, stages, MAX_CMDS);

        if (nstages==1){
            char *argv[MAX_ARGS];
            parse_argv(stages[0], argv);
            if (!argv[0]) continue;

            if (try_builtins(argv)) continue;

            pid_t pgid=0; pid_t pid = fork();
            if (pid<0){ puterr("fork failed\n"); continue; }
            if (pid==0){
                setpgid(0,0);
                if (!background) give_terminal_to(getpid());
                exec_simple(argv);
            } else {
                pgid = pid;
                setpgid(pid, pgid);
                if (!background){
                    give_terminal_to(pgid);
                    int status;
                    do {
                        if (waitpid(-pgid, &status, WUNTRACED) < 0) break;
                    } while (!(WIFEXITED(status)||WIFSIGNALED(status)||WIFSTOPPED(status)));
                    give_terminal_to(shell_pgid);

                    if (WIFSTOPPED(status)){
                        (void)add_job(pgid, 0, stages[0]);
                        job_t *j=find_job_by_pgid(pgid);
                        if (j) j->state=JOB_STOPPED;
                    }
                } else {
                    (void)add_job(pgid, 1, stages[0]);
                }
            }
        } else {
            char cmdline_copy[MAX_LINE]; s_ncpy(cmdline_copy, line, sizeof(cmdline_copy));
            (void)launch_pipeline(stages, nstages, cmdline_copy, background, NULL);
        }
    }

    /* try to terminate remaining bg jobs */
    for (int i=0;i<MAX_JOBS;i++){
        if (jobs[i].used){
            (void)kill(-jobs[i].pgid, SIGTERM);
        }
    }
    putstr("Exiting mysh...\n");
    return 0;
}

/* --------------------- Shell init --------------------- */
static void install_shell(void){
    shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    give_terminal_to(shell_pgid);
    (void)tcgetattr(STDIN_FILENO, &shell_tmodes);
    ignore_job_signals_in_shell();
}
static void give_terminal_to(pid_t pgid){
    (void)tcsetpgrp(STDIN_FILENO, pgid);
}