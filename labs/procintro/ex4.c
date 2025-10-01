#include <stdio.h>
#include <unistd.h>

int main()
{
    int i = 0, depth = 0;

    for (; i < 2 && depth < 3; i++)
    {
	depth++;

	if (fork() == 0)
	    i = -1;
	else
	    depth--;
    }

    if (depth == 3)
	printf("hi\n");
    
    return 0;
}
