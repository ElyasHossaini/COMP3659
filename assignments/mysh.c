#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_LINE 256
#define MAX_ARGS 32

// simple stdout/stderr writers
void putstr(const char *s) { write(STDOUT_FILENO, s, strlen(s)); }
void puterr(const char *s) { write(STDERR_FILENO, s, strlen(s)); }

// manual parser (whitespace split)
int parse_args(char *line, char **args) {
    int argc = 0;
    char *p = line;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        args[argc++] = p;

        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = '\0'; p++; }
    }
    args[argc] = NULL;
    return argc;
}

// PATH search + execv
void try_exec_with_path(char **args) {
    if (strchr(args[0], '/')) {
        execv(args[0], args); // absolute/relative path
        return;
    }

    char *path = getenv("PATH");
    if (!path) {
        puterr("PATH not set\n");
        _exit(1);
    }

    char buf[256];
    char *p = path;

    while (*p) {
        char *start = p;
        while (*p && *p != ':') p++;
        int len = p - start;

        if (len > 0 && len < sizeof(buf)) {
            strncpy(buf, start, len);
            buf[len] = '\0';
            strncat(buf, "/", sizeof(buf)-strlen(buf)-1);
            strncat(buf, args[0], sizeof(buf)-strlen(buf)-1);

            execv(buf, args); // try this candidate
        }

        if (*p == ':') p++;
    }
}

// simple command with < > redirection
void exec_command(char **args, int background) {
    pid_t pid = fork();

    if (pid == 0) {
        // reset SIGINT handling in child (so Ctrl-C works)
        signal(SIGINT, SIG_DFL);

        // handle I/O redirection
        for (int i = 0; args[i] != NULL; i++) {
            if (strcmp(args[i], ">") == 0 && args[i+1]) {
                int fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { puterr("open failed\n"); _exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
                args[i] = NULL;
            } else if (strcmp(args[i], "<") == 0 && args[i+1]) {
                int fd = open(args[i+1], O_RDONLY);
                if (fd < 0) { puterr("open failed\n"); _exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
                args[i] = NULL;
            }
        }

        try_exec_with_path(args);
        puterr("command not found\n");
        _exit(1);

    } else if (pid > 0) {
        if (!background) waitpid(pid, NULL, 0);
    } else {
        puterr("fork failed\n");
    }
}

// 2-stage pipeline
void exec_pipeline(char **left, char **right) {
    int pipefd[2];
    if (pipe(pipefd) < 0) { puterr("pipe failed\n"); return; }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        signal(SIGINT, SIG_DFL);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        try_exec_with_path(left);
        puterr("pipeline left failed\n");
        _exit(1);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        signal(SIGINT, SIG_DFL);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        try_exec_with_path(right);
        puterr("pipeline right failed\n");
        _exit(1);
    }

    close(pipefd[0]); close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

int main() {
    char line[MAX_LINE];
    char *args[MAX_ARGS];

    // ignore Ctrl-C in the shell itself
    signal(SIGINT, SIG_IGN);

    while (1) {
        putstr("mysh$ ");

        ssize_t n = read(STDIN_FILENO, line, sizeof(line)-1);
        if (n <= 0) break;
        line[n] = '\0';
        if (line[n-1] == '\n') line[n-1] = '\0';

        if (line[0] == '\0') continue;
        if (strcmp(line, "exit") == 0) break;

        // background job?
        int background = 0;
        int len = strlen(line);
        if (len > 0 && line[len-1] == '&') {
            background = 1;
            line[len-1] = '\0';
        }

        // pipeline?
        char *pipepos = strchr(line, '|');
        if (pipepos) {
            *pipepos = '\0';
            char *left_args[MAX_ARGS], *right_args[MAX_ARGS];
            parse_args(line, left_args);
            parse_args(pipepos+1, right_args);
            exec_pipeline(left_args, right_args);
            continue;
        }

        // normal command
        int argc = parse_args(line, args);
        if (argc == 0) continue;

        exec_command(args, background);
    }

    putstr("Exiting mysh...\n");
    return 0;
}