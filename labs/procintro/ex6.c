#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* NOTE: system calls such as fork, execve, and wait, may fail under certain
   conditoins.  If so, they often indicate failure to the caller via return
   value of -1 (but consult the manual entry for each sytem call used).
   Code should check return values and handled them appropriatley.  This
   practice is omitted from some lab exercises like this one for ease of
   illustration only.
*/

int main()
{
    pid_t pid;
    int status;
    
    char * const newargv[] = { "/bin/ls", "-al", NULL };
    char * const newenvp[] = { NULL };
    
    printf("parent about to create child...\n");

    pid = fork();

    if (pid == 0)
    {
	printf("hello, from the child about to become /bin/ls!\n");
	execve("/bin/ls", newargv, newenvp);
	printf("not reached\n");
    }

    printf("parent waiting for child to terminate...\n");
    waitpid(pid, &status, 0);
    printf("child has terminated, parent terminating...\n");
    
    return 0;
}
