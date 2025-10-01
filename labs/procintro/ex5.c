#include <stdio.h>
#include <unistd.h>

int main()
{
    char * const newargv[] = { "man", "-k", "security", NULL };
    char * const newenvp[] = { NULL };
    
    printf("I'm about to become man -k security!\n");

    if (!execve("/bin/man", newargv, newenvp))
    {
	printf("execve failed\n");
	_exit(1);
    }

    printf("not reached\n");
    return 0;
}
