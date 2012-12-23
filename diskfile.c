#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "diskfile.h"

size_t read_to_buf( diskfile_t *file, long int offset, size_t length, char *buf )
{
    FILE *fp = fopen( file->filename, "rb" );
    fseek( fp, file->offset + offset, 0 );
    memset( buf, 0, length );

    size_t max_read = file->filesize - offset; // Bytes remaining in diskfile
    size_t to_read  = MIN(length,max_read);
    size_t read = fread( buf, 1, to_read, fp );
    if ( read < to_read && !feof( fp ) )
        fputs( "Read slice error", stderr );

    fclose( fp );

    return read;
}