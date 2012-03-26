#include <stdio.h>
#include <pthread.h>

#include "common.h"

void progress_print( progress_t *progress )
{
    printf( progress->format, (float)(100 * progress->c_done) / progress->c_total, progress->w_done, progress->w_total );
    fflush( stdout );
}

progress_t *progress_init( int n_blocks, int n_slices )
{
    // Progress struct
    progress_t *progress = malloc( sizeof(progress_t) );
    progress->c_total = n_blocks * n_slices;
    progress->c_done  = 0;

    progress->w_total = n_blocks;
    progress->w_done  = 0;

    progress->mut = malloc( sizeof(pthread_mutex_t) );
    pthread_mutex_init( progress->mut, NULL );

    // Progress string
    int digits = snprintf( 0, 0, "%d", n_blocks );
    sprintf( progress->format, "Blocks Calculated [Written]: %%6.2f [%%%dd/%%%dd]\r", digits, digits );

    return progress;
}

void progress_delete( progress_t *progress )
{
    pthread_mutex_destroy( progress->mut );
    free( progress->mut );
}

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