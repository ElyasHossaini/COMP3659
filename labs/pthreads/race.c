#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>


void *thread(void *);


volatile long n = 0;
long iters;


int main(int argc, char **argv)
{
    pthread_t id1, id2;

    if (argc < 2)
	iters = 10;
    else
	iters = atol(argv[1]);

    pthread_create(&id1, 0, thread, 0);
    pthread_create(&id2, 0, thread, 0);

    pthread_join(id1, 0);
    pthread_join(id2, 0);

    printf("n = %ld\n", n);

    return 0;
}


void *thread(void *arg)
{
    long i;

    for (i = 0; i < iters; i++)
	n = n + 1;

    return 0;
}
