#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define MAX_VALUES ((unsigned long)6e8)


void random_init()
{
    // Initialize the random number generator with the current time as the seed, 
    // which ensures that we get a different sequence of random numbers each 
    // time we run the program.
    // srandom(time(NULL)); 

    // Set a fixed seed for reproducibility, i.e. will generate the same sequence
    // of random numbers every time the program is run, which is useful for 
    // debugging and testing
    srandom(2026); 
}

long random_get_value (long min, long max)
{
    return min + random() % (max - min + 1);
}


// 
// vector functions for short type
//
short *vector_create_short (unsigned long dim)
{
    return malloc(dim * sizeof(short));
}

void vector_init_short(short values[], unsigned long dim)
{
    for (unsigned long i = 0; i < dim; ++i) {
        values[i] = i + 1;
    } 
}

void vector_random_init_short(short values[], unsigned long dim)
{
    for (unsigned long i = 0; i < dim; ++i) {
        values[i] = random_get_value(SHRT_MIN, SHRT_MAX);
    } 
}


int main (int argc, char *argv[])
{
    random_init();

    unsigned long max = MAX_VALUES;

    if (argc > 1) {
        max = atol(argv[1]);
    }
    
    short *values = NULL;

    printf("Creating a vector of %lu (%.2f MB; %.2f GB) values\n", max, max/1e6, max/1e9);
    printf("This will require approximately %.2f MB (%.2f GB) of memory\n", max * sizeof(*values) / 1e6, 
                                                                            max * sizeof(*values) / 1e9);

    values = vector_create_short(max);
    if (values == NULL) {
        fprintf(stderr, "Failed to allocate memory for %lu values\n", max);
        return EXIT_FAILURE;
    }

    // vector_init_short(values, max);
    vector_random_init_short(values, max);

    long sum     = values[0];
    int  bigger  = values[0];
    int  smaller = values[0];
    for (unsigned long i = 1; i < max; ++i) {
        sum += values[i];
        if ( values[i] > bigger)  bigger = values[i];
        if ( values[i] < smaller) smaller   = values[i];
    }

    printf("smaller is %d\n",  smaller);
    printf("bigger  is %d\n",  bigger);
    printf("The sum is %ld\n", sum);

    free(values);

    return EXIT_SUCCESS;
}
