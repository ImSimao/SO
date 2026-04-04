#define _XOPEN_SOURCE 600

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_VALUES ((unsigned long)6e8)

static void random_init(void) {
    srandom(2026);  // srandom(time(NULL)); 
}

static long random_get_value(long min, long max) {
    return min + random() % (max - min + 1);
}

static short *vector_create_short(unsigned long dim) {
    return malloc(dim * sizeof(short));
}

static void vector_random_init_short(short values[], unsigned long dim) {
    for (unsigned long i = 0; i < dim; ++i) {
        values[i] = (short)random_get_value(SHRT_MIN, SHRT_MAX);
    }
}

int main(int argc, char *argv[]) {
    random_init();

    unsigned long max = MAX_VALUES;
    if (argc > 1) {
        max = strtoul(argv[1], NULL, 10);
    }
    if (max == 0) {
        fprintf(stderr, "Uso: %s <dimensao_vetor>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Creating a vector of %lu (%.2f MB; %.2f GB) values\n", max, max / 1e6, max / 1e9);
    printf("This will require approximately %.2f MB (%.2f GB) of memory\n",
           max * sizeof(short) / 1e6,
           max * sizeof(short) / 1e9);

    short *values = vector_create_short(max);
    if (values == NULL) {
        fprintf(stderr, "Failed to allocate memory for %lu values\n", max);
        return EXIT_FAILURE;
    }
    vector_random_init_short(values, max);

    struct timespec t_start;
    struct timespec t_end;
    if (clock_gettime(CLOCK_MONOTONIC, &t_start) != 0) {
        perror("clock_gettime start");
        free(values);
        return EXIT_FAILURE;
    }

    long sum = values[0];
    int bigger = values[0];
    int smaller = values[0];
    for (unsigned long i = 1; i < max; ++i) {
        sum += values[i];
        if (values[i] > bigger) bigger = values[i];
        if (values[i] < smaller) smaller = values[i];
    }

    if (clock_gettime(CLOCK_MONOTONIC, &t_end) != 0) {
        perror("clock_gettime end");
        free(values);
        return EXIT_FAILURE;
    }

    double elapsed_time =
        (t_end.tv_sec - t_start.tv_sec) + (t_end.tv_nsec - t_start.tv_nsec) * 1e-9;

    printf("smaller is %d\n", smaller);
    printf("bigger  is %d\n", bigger);
    printf("The sum is %ld\n", sum);
    printf("Total elapsed time = %9.6lfs (%9.6lfus)\n",
           elapsed_time,
           elapsed_time * 1e6);

    free(values);
    return EXIT_SUCCESS;
}
