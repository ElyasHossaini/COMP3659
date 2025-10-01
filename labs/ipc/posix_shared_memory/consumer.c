#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include "shared.h"

void consume_item(Item item);

int main()
{
    Item item;
    int fd;
    Shared_Data *shared_data;

    srandom(time(NULL));     /* seed random number generator */

    /* (1) open pre-exiting shared member object (created by producer)
       (2) map it into this process's addrss space starting at address "shared_data"
    */

    fd = shm_open("prodcon", O_RDWR, 0666);
    shared_data = mmap(0, SHARED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    while (1)     /* repeatedly (forever) dequeue and consume items from the bounded buffer */
    {
	while (shared_data->in == shared_data->out)
	    ;     /* do nothing - busy wait if buffer empty */

	item = shared_data->buffer[shared_data->out];                /* dequeue */
	shared_data->out = (shared_data->out + 1) % BUFFER_SIZE;

	printf("consuming item %d\n", item);

	consume_item(item);
    }
    
    /* not reached */
    return 0;
}

void consume_item(Item item)
{
    long i, upper = random() % 100000000L + 2000000L;

    /* simulate some time-consuming, variable-time item consumption process */

    for (i = 0L; i < upper; i++)
	;
}
