#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>


typedef int Item;     /* some item type - doesn't matter what it is */


void *producer(void *);
Item produce_item();

void *consumer(void *);
void consume_item(Item item);


/* the producer and consumer will share a bounded buffer via the following
   global variables.

   The bounded buffer can hold between 0 and BUFFER_SIZE-1 (3) Items
   (in == out) will indicate buffer empty
   (((in + 1) % BUFFER_SIZE) == out) will indicate buffer full
*/

#define BUFFER_SIZE 10                  /* size of bounded buffer */

volatile Item buffer[BUFFER_SIZE];     /* the bounded buffer */
volatile int in;                       /* index of next enqueue */
volatile int out;                      /* index of next dequeue */


int main()
{
    pthread_t id1, id2;
    
    srandom(time(NULL));     /* seed random number generator */

    in = 0;      /* initialize the shared data structure (a bounded buffer) */
    out = 0;

    pthread_create(&id1, 0, producer, 0);     /* create producer thread */
    pthread_create(&id2, 0, consumer, 0);     /* create consumer thread */

    pthread_join(id1, 0);     /* neither terminates, so this blocks forever */
    pthread_join(id2, 0);     /* not reached */
    return 0;
}


void *producer(void *arg)     /* this function runs in its own thread */
{
    Item item;

    while (1)     /* repeatedly (forever) produce and enqueue items in the bounded buffer */
    {
	item = produce_item();

	while (((in + 1) % BUFFER_SIZE) == out)
	    ;     /* do nothing - busy wait if buffer full */

	printf("producing item %d\n", item);

	buffer[in] = item;               /* enqueue */
	in = (in + 1) % BUFFER_SIZE;
    }
}


Item produce_item()
{
    static int item = 0;
    long i, upper = random() % 1000000000L;

    /* simulate some time-consuming, variable-time item generation process */

    for (i = 0L; i < upper; i++)
	;

    return item++;
}


void *consumer(void *arg)     /* this function runs in its own thread */
{
    Item item;

    while (1)     /* repeatedly (forever) dequeue and consume items from the bounded buffer */
    {
	while (in == out)
	    ;     /* do nothing - busy wait if buffer empty */

	item = buffer[out];                /* dequeue */
	out = (out + 1) % BUFFER_SIZE;

	printf("consuming item %d\n", item);

	consume_item(item);
    }
}


void consume_item(Item item)
{
    long i, upper = random() % 1000000000L;

    /* simulate some time-consuming, variable-time item consumption process */

    for (i = 0L; i < upper; i++)
	;
}
