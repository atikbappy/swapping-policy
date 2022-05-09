#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include "harness.h"

double Time_GetSeconds() {
    struct timeval t;
    int rc = gettimeofday(&t, NULL);
    assert(rc == 0);
    return (double) ((double)t.tv_sec + (double)t.tv_usec / 1e6);
}

/* A simple test program.
 * You should modify this to perform additional checks
 */

int main(int argc, char ** argv) {
    FILE * fp;
    fp = freopen("input.txt", "r", stdin);

    if (argc != 2) {
        fprintf(stderr, "usage: spin <memory (MB)>\n");
        exit(1);
    }
    long long int size = (long long int) atoi(argv[1]);
    long long int size_in_bytes = size * 1024 * 1024;

    printf("allocating %lld bytes (%.2f MB)\n",
           size_in_bytes, size_in_bytes / (1024 * 1024.0));


    /* this will setup the signal handler to take care of seg fault */
    init_petmem();

    int *x = pet_malloc(size_in_bytes);
    if (x == NULL) {
        fprintf(stderr, "memory allocation failed\n");
        exit(1);
    }

    long long int num_ints = size_in_bytes / sizeof(int);
    printf("  number of integers in array: %lld\n", num_ints);

    long long int i = 0, index, j;
    double time_since_last_print = 2.0;
    double t = Time_GetSeconds();
    int loop_count = 0;

    int * indices;
    indices = (int *)malloc(num_ints * sizeof(int));
    for (j=0; j<num_ints; j++) {
        scanf("%d", &indices[j]);
    }

    while (1) {
        x[indices[i]] += 1;
        i++;

        if (i == num_ints) {
            double delta_time = Time_GetSeconds() - t;
            time_since_last_print += delta_time;
            if (time_since_last_print >= 0.2) { // only print every .2 seconds
                printf("loop %d in %.2f ms (bandwidth: %.2f MB/s)\n",
                       loop_count, 1000 * delta_time,
                       size_in_bytes / (1024.0*1024.0*delta_time));
                time_since_last_print = 0;
            }

            i = 0;
            t = Time_GetSeconds();
            loop_count++;
        }
    }

    fclose(fp);
    pet_free(x);

    return 0;
}

/* vim: set ts=4: */
