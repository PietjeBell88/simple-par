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

typedef struct
{
    char format[100];
    int c_total;   // blocks*slices;
    int c_done;    // part of c_total that has been calculated

    int w_total;   // blocks
    int w_done;    // blocks that have been written out
} progress_t;


void progress_print( progress_t *progress );

progress_t *progress_init( int n_blocks, int n_slices );

void progress_delete( progress_t *progress );

inline size_t FILESIZE(char *fname)
{
    FILE *f = fopen( fname, "r" );
    fseek( f, 0, SEEK_END );
    size_t size = ftell( f );
    fclose( f );
    return size;
}

inline char* strdup( const char * s )
{
    size_t len = 1 + strlen(s);
    char *p = malloc( len );
    return p ? memcpy( p, s, len ) : NULL;
}

#endif