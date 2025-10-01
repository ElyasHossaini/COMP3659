#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 256
#define MAX_ARGS 32

//write a string to stdout
void putstr(const char *s) {
	write(STDOUT_FILENO, s, strlen(s));
}

void puterr(const char *s) {
	write(STDERR_FILENO, s, strlen(s));
}

int parse_args(char *line, char **args) {
	int argc = 0;
	char *p = line;

	while(*p != '\0') {
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

void exec_command(char **args, int background) {
	pid_t pid = fork();

	if(pid == 0) {
		for(int i = 0; args[i] != NULL; i++) {
			if (strcmp(args[i], ">") == 0) {
				int fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (fd < 0) {
					puterr("open failed\n");
					_exit(1);
				}
				dup2(fd, STDOUT_FILENO);
				close(fd);
				args[i] = NULL;
			}
			else if (strcmp(args[i], "<") == 0) {
				int fd = open(args[i+1], O_RDONLY);
				if (fd < 0) {
					puterr("open failed\n");
					_exit(1);
				}
				dup2(fd, STDIN_FILENO);
				close(fd);
				args[i] = NULL;
			}

			execvp(args[0], args);
			puterr("command not found\n");
			_exit(1);
		}
		else if
	}
}

int main() {
	char line[MAX_LINE];
	char *args[MAX_ARGS];

	while(1) {
		putstr("mysh$ ");

		ssize_t n = read(STDIN_FILENO, line, sizeof(line) - 1);
		if (n <= 0) {
			break;
		}
		line[n] = '\0';
		
		if (line[n-1] == '\n') {
			line[n-1] = '\0';
		}

		if (line[0] == '\0') {
			continue;
		}

		if (strcmp(line, "exit") == 0) {
			break;
		}

		int argc = 0;
		char *token = strtok(line, " \t");
		while (token != NULL && argc < MAX_ARGS - 1) {
			args[argc++] = token;
			token = strtok(NULL, " \t");
		}
		args[argc] = NULL;

		pid_t pid = fork();
		if (pid == 0) {
			execvp(args[0], args);

			putstr("error: command not found\n");
			_exit(1);
		}
		else if (pid > 0) {
			
			waitpid(pid, NULL, 0);
		}
		else {
			putstr("error: fork failed\n");
		}
	}

	putstr("exiting mysh...\n");
	return 0;

}
