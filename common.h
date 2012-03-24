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

size_t FILESIZE(char *fname)
{
    FILE *f = fopen( fname, "r" );
    fseek( f, 0, SEEK_END );
    size_t size = ftell( f );
    fclose( f );
    return size;
}

char* strdup( const char * s )
{
    size_t len = 1 + strlen(s);
    char *p = malloc( len );
    return p ? memcpy( p, s, len ) : NULL;
}

#endif