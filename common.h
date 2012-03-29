#ifndef SPAR_COMMON_H
#define SPAR_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a,b) ({ \
    a > b ? b : a; \
})

#define MAX(a,b) ({ \
    a > b ? a : b; \
})

// Define macro for aligning data
#define ALIGNED_16(X) X __attribute__((aligned(16)))

typedef struct
{
    char format[100];
    int c_total;   // blocks*slices;
    int c_done;    // part of c_total that has been calculated

    int w_total;   // blocks
    int w_done;    // blocks that have been written out

    pthread_mutex_t *mut;
} progress_t;


void progress_print( progress_t *progress );

progress_t *progress_init( int n_blocks, int n_slices );

void progress_delete( progress_t *progress );

size_t FILESIZE(char *fname);

char* strdup( const char * s );

#endif