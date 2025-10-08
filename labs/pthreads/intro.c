#include <stdio.h>
#include <pthread.h>
#include <time.h>


void *thread1(void *);
void *thread2(void *);
void pause();


int main()
{
    pthread_t id1, id2;

    printf("hit enter to start thread 1...\n");
    getchar();
    pthread_create(&id1, 0, thread1, 0);

    printf("hit enter to start thread 2...\n");
    getchar();
    pthread_create(&id2, 0, thread2, 0);

    pthread_join(id1, 0);     /* wait for threads to terminate */
    pthread_join(id2, 0);

    return 0;
}


void *thread1(void *arg)
{
    int i;

    for (i = 0; i < 10; i++)
    {
	printf("\thello from thread 1\n");
	pause();
    }

    printf("thread 1 terminating\n");
    return 0;
}


void *thread2(void *arg)
{
    int i;

    for (i = 0; i < 10; i++)
    {
	printf("\thello from thread 2\n");
	pause();
    }

    printf("thread 2 terminating\n");
    return 0;
}


void pause()
{
    struct timespec t = { 1, 0 };
    nanosleep(&t, 0);
}
