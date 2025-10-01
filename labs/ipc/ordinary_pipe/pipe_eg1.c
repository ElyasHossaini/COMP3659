#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#define READ_END  0
#define WRITE_END 1

#define BUFF_LEN 256

int main()
{
    int pipefd[2];
    int pid;
    int child_status;
    int bytes_read;
    char buffer[BUFF_LEN];
    char *message = "The message sent down the pipe!";

    if (pipe(pipefd) == -1)     /* create a pipe */
    {
	perror("pipe failed");
	exit(EXIT_FAILURE);
    }

    pid = fork();     /* spawn a child process */

    if (pid == -1)
    {
	perror("fork failed");
	exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
	/* we're the child process - we'll read a message from the pipe */

	close(pipefd[WRITE_END]);     /* close the end we're not using */

	bytes_read = read(pipefd[READ_END], buffer, BUFF_LEN-1);
	
	if (bytes_read == -1)
	{
	    perror ("read failed");
	    exit(EXIT_FAILURE);
	}

	buffer[bytes_read] = '\0';
	printf("hello from child: read \"%s\" from the pipe\n", buffer);

	close(pipefd[READ_END]);
    }
    else
    {
	/* we're the parent process - we'll write a message to the pipe */

	close(pipefd[READ_END]);     /* close the end we're not using */

	printf("hello from parent: about to write \"%s\" to the pipe...\n", message);
	write(pipefd[WRITE_END], message, strlen(message));

	waitpid(pid, &child_status, 0);     /* wait for child to terminate */
	close(pipefd[WRITE_END]);
    }

    exit(EXIT_SUCCESS)
}
