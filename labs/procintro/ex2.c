#include <stdio.h>
#include <unistd.h>

int main()
{
    pid_t pid;
    printf("hello from parent! %d \n", getpid());
    sleep(20);	
    pid = fork();     /* called once (in parent) */
                      /* returns in parent; also "returns" in new child! */

    if (pid == -1)
    {
	printf("fork failed\n");
	_exit(1);
    }

    if (pid == 0)
    {
	printf("hello from child! %d \n", getpid());
        sleep(10);
    }
    else
    {
	printf("hello again from parent! %d \n", getpid());
	sleep(2);
    }
    
    _exit(0);
}
