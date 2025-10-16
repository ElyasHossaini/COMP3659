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
static void putstr(const char *s){ write(STDOUT_FILENO, s, strlen(s)); }
static void puterr(const char *s){ write(STDERR_FILENO, s, strlen(s)); }

/* --------------------- Job control --------------------- */
typedef enum { JOB_RUNNING=0, JOB_STOPPED=1, JOB_DONE=2 } job_state_t;
typedef struct {
    int used;
    int id;                /* 1-based */
    pid_t pgid;            /* process group id */
    int background;        /* 1 = bg, 0 = fg */
    job_state_t state;
    char cmdline[MAX_LINE];
} job_t;

static job_t jobs[MAX_JOBS];
static pid_t shell_pgid = 0;
static struct termios shell_tmodes;

/* forward decls */
static void install_shell();
static void mark_job_state(pid_t pid, int status);
static int  add_job(pid_t pgid, int bg, const char *cmdline);
static job_t* find_job_by_id(int id);
static job_t* find_job_by_pgid(pid_t pgid);
static void remove_job(job_t *j);
static void print_job(const job_t *j);

/* --------------------- Tiny utils --------------------- */
static void trim_trailing(char *s){
    int n = (int)strlen(s);
    while(n>0 && (s[n-1]==' '||s[n-1]=='\t')){ s[n-1]='\0'; n--; }
}
static void skip_ws(char **p){ while(**p==' '||**p=='\t') (*p)++; }

/* split into argv by spaces/tabs, no quotes */
static int parse_argv(char *s, char **argv){
    int argc=0;
    char *p = s;
    while (*p) {
        skip_ws(&p);
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p!=' ' && *p!='\t') p++;
        if (*p) { *p = '\0'; p++; }
        if (argc >= MAX_ARGS-1) break;
    }
    argv[argc] = NULL;
    return argc;
}

/* split pipeline into command substrings by '|' */
static int split_pipeline(char *line, char *stages[], int max){
    int n=0; char *p=line;
    while (*p) {
        stages[n++] = p;
        while (*p && *p!='|') p++;
        if (*p=='|'){ *p='\0'; p++; }
        skip_ws(&p);
        if (n>=max) break;
    }
    return n;
}

/* safe string copy (bounded) */
static void s_ncpy(char *dst, const char *src, size_t n){
    size_t i=0; for(; i+1<n && src[i]; i++) dst[i]=src[i]; dst[i]='\0';
}

/* --------------------- PATH search (no execvp) --------------------- */
static void try_exec_with_path(char **argv){
    if (strchr(argv[0], '/')) {
        execv(argv[0], argv);
        _exit(127);
    }
    char *path = getenv("PATH");
    if (!path) { _exit(127); }

    char buf[512];
    char *p = path;
    while (*p) {
        char *start = p;
        while (*p && *p!=':') p++;
        int len = (int)(p - start);
        if (len > 0 && len < (int)sizeof(buf)-1) {
            int off=0;
            if (len >= (int)sizeof(buf)-1) len = (int)sizeof(buf)-2;
            for (int i=0;i<len;i++) buf[off++]=start[i];
            if (off< (int)sizeof(buf)-1) buf[off++]='/';
            const char *nm = argv[0];
            while (*nm && off < (int)sizeof(buf)-1) buf[off++]=*nm++;
            buf[off]='\0';
            execv(buf, argv); /* try */
        }
        if (*p==':') p++;
    }
    _exit(127);
}

/* --------------------- Signals --------------------- */
static volatile sig_atomic_t sigchld_seen = 0;

static void sigchld_handler(int sig){
    (void)sig;
    sigchld_seen = 1;
    /* Reap here in a loop; keep it async-signal-safe (avoid stdio). */
    int status; pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) > 0) {
        mark_job_state(pid, status);
    }
}

static void ignore_job_signals_in_shell(void){
    signal(SIGINT,  SIG_IGN); /* ^C */
    signal(SIGTSTP, SIG_IGN); /* ^Z */
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    struct sigaction sa; memset(&sa,0,sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP | SA_NOCLDWAIT; /* periodic cleanup ok */
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

static void mark_job_state(pid_t pid, int status){
    (void)pid;
    /* We only know pid; map to pgid via /proc would be overkill. Rely on waitpid(-pgid,..) elsewhere.
       Here, conservatively mark DONE if reaped, or STOPPED/CONTINUED if flags say so. */
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        /* any child ended; scan jobs and mark DONE if their processes are finished.
           Simplify: mark job DONE (shell prints on 'jobs' or reaping loop clears). */
        for (int i=0;i<MAX_JOBS;i++){
            if (jobs[i].used && jobs[i].state!=JOB_DONE) {
                /* We'll verify emptiness when parent waits on fg or on bg reaping. */
                jobs[i].state = JOB_DONE;
            }
        }
    } else if (WIFSTOPPED(status)) {
        for (int i=0;i<MAX_JOBS;i++) if (jobs[i].used) jobs[i].state = JOB_STOPPED;
    } else if (WIFCONTINUED(status)) {
        for (int i=0;i<MAX_JOBS;i++) if (jobs[i].used) jobs[i].state = JOB_RUNNING;
    }
}
static void reap_done_jobs(void){
    /* Passive cleanup to avoid zombies even if no signals delivered (e.g., if SA_NOCLDWAIT not honored) */
    int status; pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0){
        (void)pid; /* status already reflected by mark_job_state in handler. */
    }
    /* Remove DONE jobs from table */
    for (int i=0;i<MAX_JOBS;i++){
        if (jobs[i].used && jobs[i].state==JOB_DONE){
            remove_job(&jobs[i]);
        }
    }
}
static void print_job(const job_t *j){
    if (!j || !j->used) return;
    char buf[64];
    char *st = (j->state==JOB_RUNNING? "Running" : (j->state==JOB_STOPPED? "Stopped" : "Done"));
    int len = 0;
    /* format: [id]  pgid  State: cmdline\n */
    len += snprintf(NULL,0,"[%d] %d %s: ", j->id, (int)j->pgid, st); /* can't use snprintf; build manually */
    /* manual minimal format without stdio */
    putstr("[");
    char tmp[16]; int k=0; int id=j->id; if (id==0){ tmp[k++]='0'; }
    else { char rev[16]; int r=0; while(id>0){ rev[r++]= (char)('0'+(id%10)); id/=10; }
           while (r--) tmp[k++]=rev[r]; }
    tmp[k]='\0'; putstr(tmp); putstr("] ");

    /* pgid */
    k=0; int pg=(int)j->pgid; if (pg==0){ tmp[k++]='0'; }
    else { char rev[16]; int r=0; while(pg>0){ rev[r++]= (char)('0'+(pg%10)); pg/=10; }
           while (r--) tmp[k++]=rev[r]; }
    tmp[k]='\0'; putstr(tmp); putstr(" ");

    putstr(st); putstr(": ");
    putstr(j->cmdline); putstr("\n");
}

/* --------------------- Terminal ownership --------------------- */
static void give_terminal_to(pid_t pgid){
    tcsetpgrp(STDIN_FILENO, pgid);
}
static void install_shell(){
    shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    tcgetattr(STDIN_FILENO, &shell_tmodes);
    ignore_job_signals_in_shell();
}

/* --------------------- Builtins: exit, cd, jobs, fg, bg --------------------- */
static int is_number(const char *s){
    if (!s || !*s) return 0;
    for (const char *p=s; *p; ++p) if (*p<'0'||*p>'9') return 0;
    return 1;
}

static int builtin_cd(char **argv){
    const char *dir = argv[1] ? argv[1] : getenv("HOME");
    if (!dir) { puterr("cd: HOME not set\n"); return -1; }
    if (chdir(dir) < 0) { puterr("cd: failed\n"); return -1; }
    return 0;
}

static void builtin_jobs(void){
    for (int i=0;i<MAX_JOBS;i++) if (jobs[i].used) print_job(&jobs[i]);
}

static void resume_job_bg(job_t *j){
    if (!j) { puterr("bg: no such job\n"); return; }
    j->background = 1;
    j->state = JOB_RUNNING;
    kill(-j->pgid, SIGCONT);
}

static void resume_job_fg(job_t *j){
    if (!j){ puterr("fg: no such job\n"); return; }
    j->background = 0;
    j->state = JOB_RUNNING;
    give_terminal_to(j->pgid);
    kill(-j->pgid, SIGCONT);
    /* Wait for foreground job to finish or stop */
    int status; pid_t w;
    do {
        w = waitpid(-j->pgid, &status, WUNTRACED);
        if (w==-1 && errno==ECHILD) break;
    } while (!(WIFEXITED(status) || WIFSIGNALED(status) || WIFSTOPPED(status)));
    /* Return terminal to shell */
    give_terminal_to(shell_pgid);
    if (WIFEXITED(status)||WIFSIGNALED(status)) { /* done */
        remove_job(find_job_by_pgid(j->pgid));
    } else if (WIFSTOPPED(status)) {
        job_t *jj = find_job_by_pgid(j->pgid);
        if (jj) jj->state = JOB_STOPPED;
    }
}

static int try_builtins(char **argv){
    if (!argv[0]) return 1;
    if (strcmp(argv[0],"exit")==0) _exit(0);
    if (strcmp(argv[0],"cd")==0)   { builtin_cd(argv); return 1; }
    if (strcmp(argv[0],"jobs")==0) { builtin_jobs();  return 1; }
    if (strcmp(argv[0],"bg")==0) {
        /* bg [%job] or bg <id> */
        job_t *j=NULL;
        if (argv[1] && argv[1][0]=='%' && is_number(argv[1]+1)) j = find_job_by_id(atoi(argv[1]+1));
        else if (argv[1] && is_number(argv[1])) j = find_job_by_id(atoi(argv[1]));
        else { /* pick most recent stopped */
            for (int i=MAX_JOBS-1;i>=0;i--) if (jobs[i].used && jobs[i].state==JOB_STOPPED){ j=&jobs[i]; break; }
        }
        resume_job_bg(j);
        return 1;
    }
    if (strcmp(argv[0],"fg")==0) {
        job_t *j=NULL;
        if (argv[1] && argv[1][0]=='%' && is_number(argv[1]+1)) j = find_job_by_id(atoi(argv[1]+1));
        else if (argv[1] && is_number(argv[1])) j = find_job_by_id(atoi(argv[1]));
        else { /* pick most recent non-done */
            for (int i=MAX_JOBS-1;i>=0;i--) if (jobs[i].used && jobs[i].state!=JOB_DONE){ j=&jobs[i]; break; }
        }
        resume_job_fg(j);
        return 1;
    }
    return 0;
}

/* --------------------- Exec of one command (with <, >) --------------------- */
static void apply_redirs(char **argv){
    for (int i=0; argv[i]; ++i){
        if (strcmp(argv[i],">")==0 && argv[i+1]) {
            int fd = open(argv[i+1], O_WRONLY|O_CREAT|O_TRUNC, 0644);
            if (fd<0){ puterr("open > failed\n"); _exit(1); }
            dup2(fd, STDOUT_FILENO); close(fd);
            argv[i]=NULL; break;
        } else if (strcmp(argv[i],"<")==0 && argv[i+1]) {
            int fd = open(argv[i+1], O_RDONLY);
            if (fd<0){ puterr("open < failed\n"); _exit(1); }
            dup2(fd, STDIN_FILENO); close(fd);
            argv[i]=NULL; break;
        }
    }
}

static void exec_simple(char **argv){
    apply_redirs(argv);
    /* In child: default signal handlers so ^C/^Z affect the job */
    signal(SIGINT, SIG_DFL);
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
        if (s==0 && try_builtins(argv)) { /* built-in alone (no pipe) */
            /* If there are multiple stages and first is builtin, we don't support piping from builtins here (keep simple). */
            if (nstages==1) return 0;
        }

        pid_t pid = fork();
        if (pid<0){ puterr("fork failed\n"); return -1; }

        if (pid==0){
            /* child */
            if (pgid==0) pgid=getpid();
            setpgid(getpid(), pgid);

            /* I/O setup for this stage */
            if (s>0) { dup2(pipes[s-1][0], STDIN_FILENO); }
            if (s<nstages-1) { dup2(pipes[s][1], STDOUT_FILENO); }
            for (int i=0;i<nstages-1;i++){ close(pipes[i][0]); close(pipes[i][1]); }

            exec_simple(argv); /* never returns */
        } else {
            /* parent */
            if (pgid==0) pgid=pid;
            setpgid(pid, pgid);
        }
    }

    for (int i=0;i<nstages-1;i++){ close(pipes[i][0]); close(pipes[i][1]); }

    if (!background){
        give_terminal_to(pgid);
        int status; pid_t w;
        do {
            w = waitpid(-pgid, &status, WUNTRACED);
            if (w==-1 && errno==ECHILD) break;
        } while (!(WIFEXITED(status) || WIFSIGNALED(status) || WIFSTOPPED(status)));
        give_terminal_to(shell_pgid);

        if (WIFSTOPPED(status)){
            int jid = add_job(pgid, 0, cmdline);
            (void)jid;
            job_t *j = find_job_by_pgid(pgid);
            if (j) j->state = JOB_STOPPED;
        } else {
            /* finished; nothing to add to jobs */
        }
    } else {
        int jid = add_job(pgid, 1, cmdline);
        (void)jid;
    }
    if (out_pgid) *out_pgid = pgid;
    return pgid;
}

/* --------------------- Read line (raw, system calls) --------------------- */
static int read_line(char *buf, int maxlen){
    int off=0;
    while (off < maxlen-1){
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
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
        if (n<=0) { putstr("\n"); break; }

        /* trim newline & trailing spaces; handle empty */
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

        /* quick built-in path for single command (no pipes) */
        char *pipepos = strchr(line,'|');

        /* Parse pipeline stages */
        char *stages[MAX_CMDS];
        int nstages = split_pipeline(line, stages, MAX_CMDS);

        if (nstages==1){
            /* single command path: try builtins first */
            char *argv[MAX_ARGS];
            parse_argv(stages[0], argv);
            if (!argv[0]) continue;

            if (try_builtins(argv)) {
                /* built-in done; continue loop */
                continue;
            }

            /* external command (possibly with redirs) */
            pid_t pgid = 0;
            pid_t pid = fork();
            if (pid<0){ puterr("fork failed\n"); continue; }
            if (pid==0){
                /* child: its own process group */
                setpgid(0,0);
                if (!background) give_terminal_to(getpid());
                exec_simple(argv);
            } else {
                /* parent */
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
                        int jid = add_job(pgid, 0, stages[0]);
                        (void)jid;
                        job_t *j=find_job_by_pgid(pgid);
                        if (j) j->state=JOB_STOPPED;
                    }
                } else {
                    int jid = add_job(pgid, 1, stages[0]);
                    (void)jid;
                }
            }
        } else {
            /* n-stage pipeline */
            char cmdline_copy[MAX_LINE]; s_ncpy(cmdline_copy, line, sizeof(cmdline_copy));
            (void)pipepos;
            pid_t pgid=0;
            launch_pipeline(stages, nstages, cmdline_copy, background, &pgid);
        }
    }

    /* On exit, try to terminate any remaining bg jobs politely */
    for (int i=0;i<MAX_JOBS;i++){
        if (jobs[i].used){
            kill(-jobs[i].pgid, SIGTERM);
        }
    }
    putstr("Exiting mysh...\n");
    return 0;
}