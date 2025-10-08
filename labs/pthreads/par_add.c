#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>


struct range
{
    int depth;
    long lower;
    long upper;
};


void *par_add(void *);


int main(int argc, char **argv)
{
    struct range top_level;

    if (argc < 3)
    {
	fprintf(stderr, "usage: %s add-to depth\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    top_level.depth = atoi(argv[2]);
    top_level.lower = 1;
    top_level.upper = atol(argv[1]);

    printf("sum of 1 to %ld = %ld\n", top_level.upper, (long)par_add(&top_level));
    
    return 0;
}


void *par_add(void *arg)
{
    long sum, sum_left, sum_right, next;
    pthread_t left, right;
    struct range range_left, range_right, *rangep = (struct range *)arg;

    if (rangep->depth > 1 && rangep->lower < rangep->upper)
    {
	range_left.depth = rangep->depth - 1;
	range_left.lower = rangep->lower;
	range_left.upper = (rangep->lower + rangep->upper) / 2;
	
	range_right.depth = rangep->depth - 1;
	range_right.lower = range_left.upper + 1;
	range_right.upper = rangep->upper;
	
	printf("summing %ld to %ld\n", range_left.lower, range_left.upper); 
        pthread_create(&left, 0, par_add, &range_left);
	printf("summing %ld to %ld\n", range_right.lower, range_right.upper); 
	pthread_create(&right, 0, par_add, &range_right);

	pthread_join(left, (void **)&sum_left);
	pthread_join(right, (void **)&sum_right);

	sum = sum_left + sum_right;
    }
    else
    {
	sum = 0;

	for (next = rangep->lower; next <= rangep->upper; next++)
	    sum += next;
    }

    return (void *)sum;
}
