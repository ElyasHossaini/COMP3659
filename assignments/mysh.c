#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 256
#define MAX_ARGS 32

// simple write wrapper
void putstr(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
}

// simple error writer
void puterr(const char *s) {
    write(STDERR_FILENO, s, strlen(s));
}

// manual parser (splits by spaces/tabs)
int parse_args(char *line, char **args) {
    int argc = 0;
    char *p = line;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        args[argc++] = p;

        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }

    args[argc] = NULL;
    return argc;
}

// execute a simple command with optional < > redirection
void exec_command(char **args, int background) {
    pid_t pid = fork();

    if (pid == 0) {
        // child
        for (int i = 0; args[i] != NULL; i++) {
            if (strcmp(args[i], ">") == 0) {
                int fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { puterr("open failed\n"); _exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
                args[i] = NULL;
            }
            else if (strcmp(args[i], "<") == 0) {
                int fd = open(args[i+1], O_RDONLY);
                if (fd < 0) { puterr("open failed\n"); _exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
                args[i] = NULL;
            }
        }

        execvp(args[0], args);
        puterr("command not found\n");
        _exit(1);

    } else if (pid > 0) {
        // parent
        if (!background) {
            waitpid(pid, NULL, 0);
        }
    } else {
        puterr("fork failed\n");
    }
}

// execute a simple pipeline: cmd1 | cmd2
void exec_pipeline(char **left, char **right) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        puterr("pipe failed\n");
        return;
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(left[0], left);
        puterr("pipeline left failed\n");
        _exit(1);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(right[0], right);
        puterr("pipeline right failed\n");
        _exit(1);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

int main() {
    char line[MAX_LINE];
    char *args[MAX_ARGS];

    while (1) {
        putstr("mysh$ ");

        ssize_t n = read(STDIN_FILENO, line, sizeof(line)-1);
        if (n <= 0) break;
        line[n] = '\0';
        if (line[n-1] == '\n') line[n-1] = '\0';

        if (line[0] == '\0') continue;
        if (strcmp(line, "exit") == 0) break;

        // background?
        int background = 0;
        int len = strlen(line);
        if (len > 0 && line[len-1] == '&') {
            background = 1;
            line[len-1] = '\0';
        }

        // check for pipeline
        char *pipepos = strchr(line, '|');
        if (pipepos) {
            *pipepos = '\0';
            char *left_args[MAX_ARGS];
            char *right_args[MAX_ARGS];
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