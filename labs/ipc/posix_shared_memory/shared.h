#ifndef SHARED_H
#define SHARED_H

#define BUFFER_SIZE 4               /* size of bounded buffer */
#define SHARED_MEMORY_SIZE 4096

typedef int Item;     /* some item type - doesn't matter what it is */

/* the producer and consumer will share a data structure of the following
   type (it will resize in a shared memory segment).

   The bounded buffer can hold between 0 and BUFFER_SIZE-1 (3) Items
   (in == out) will indicate buffer empty
   (((in + 1) % BUFFER_SIZE) == out) will indicate buffer full
*/

typedef struct
{
    Item buffer[BUFFER_SIZE];     /* the bounded buffer */
    int in;                       /* index of next enqueue */
    int out;                      /* index of next dequeue */
}
    Shared_Data;

#endif
