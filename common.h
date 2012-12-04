#ifndef SPAR_COMMON_H
#define SPAR_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "osdep.h"

#define MIN(a,b) ({ \
    a > b ? b : a; \
})

#define MAX(a,b) ({ \
    a > b ? a : b; \
})

typedef unsigned char md5_t[16];

typedef uint32_t crc32_t;

#pragma pack(1)
typedef struct {
    md5_t md5;
    crc32_t crc;
} checksum_t;
#pragma pack(0)


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

char* strdup2( const char * s );

void *spar2_malloc( int );

void  spar2_free( void * );

#endif