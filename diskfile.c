#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "diskfile.h"
#include "spar2.h"
#include "common.h"

void spar_add_input_file( spar_param_t *param, char *filename, char *virtual_filename, size_t offset, size_t filesize )
{
    // Make some room for this input file
    spar_diskfile_t *backup = param->input_files;
    param->input_files = malloc( (param->n_input_files + 1) * sizeof(spar_diskfile_t) );
    if( param->n_input_files > 0 )
    {
        memcpy( param->input_files, backup, sizeof(spar_diskfile_t) );
        free( backup );
    }

    spar_diskfile_t * df = &param->input_files[param->n_input_files];

    param->n_input_files++;

    df->filename = strdup( filename );

    if( virtual_filename != NULL )
        df->virtual_filename = strdup( virtual_filename );
    else
        df->virtual_filename = NULL;

    df->offset = offset;

    df->filesize = filesize;
}

size_t read_to_buf( spar_diskfile_t *file, long int offset, size_t length, char *buf )
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