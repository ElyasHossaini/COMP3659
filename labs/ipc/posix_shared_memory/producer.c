#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include "shared.h"

Item produce_item();

int main()
{
    Item item;
    int fd;
    Shared_Data *shared_data;

    srandom(time(NULL));     /* seed random number generator */

    /* (1) create shared memory object
       (2) set its size to 4k
       (3) map it into this process's addrss space starting at address "shared_data"
    */

    fd = shm_open("prodcon", O_CREAT | O_RDWR, 0666);
    ftruncate(fd, SHARED_MEMORY_SIZE);
    shared_data = mmap(0, SHARED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    /* from here on, treat the shared memory region as an instance of the Shared_Data struct */
    
    shared_data->in = 0;      /* initialize the shared data structure (a bounded buffer) */
    shared_data->out = 0;

    while (1)     /* repeatedly (forever) produce and enqueue items in the bounded buffer */
    {
	item = produce_item();

	while (((shared_data->in + 1) % BUFFER_SIZE) == shared_data->out)
	    ;     /* do nothing - busy wait if buffer full */

	printf("producing item %d\n", item);

	shared_data->buffer[shared_data->in] = item;               /* enqueue */
	shared_data->in = (shared_data->in + 1) % BUFFER_SIZE;
    }
    
    /* not reached */
    return 0;
}

Item produce_item()
{
    static int item = 0;
    long i, upper = random() % 100000000L + 2000000L;

    /* simulate some time-consuming, variable-time item generation process */

    for (i = 0L; i < upper; i++)
	;

    return item++;
}
