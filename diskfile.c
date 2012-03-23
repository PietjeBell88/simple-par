#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "diskfile.h"

void read_to_buf( diskfile_t *file, long int offset, size_t length, char *buf )
{
    FILE *fp = fopen( file->filename, "rb" );
    fseek( fp, file->offset + offset, 0 );
    memset( buf, 0, length );
    int read = fread( buf, 1, length, fp );
    if ( read < length && !feof( fp ) )
    {
        fputs( "Read slice error", stderr );
    }
    fclose( fp );
}