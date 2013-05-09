#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <getopt.h>

#include "spar2.h"
#include "par2.h"
#include "diskfile.h"
#include "common.h"
#include "reedsolomon.h"

#include "extern/crc32.h"
#include "extern/md5.h"

static void spar_print_version()
{
    printf( "spar2 version: \"%s\"\n\n", SPAR_VERSION );
    printf( "It's like par2cmdline but with less features:\n" );
    printf( " - Only creation of par2 files. (no repair or verification)\n" );
    printf( " - Only limited size exponential scheme supported (-l in par2cmdline).\n" );
    printf( " - Only block size and redundancy can be set.\n\n" );
    printf( "Made possible by \"Bored at Work\"^TM.\n" );
}

static void help( spar_param_t *param )
{
    printf( "spar2 version: \"%s\"\n\n", SPAR_VERSION );
    printf( "Usage: spar2 c(reate) [options] <par2 file> [files]\n\n" );
    printf( "Options:\n" );
    printf( "  --help,           -h     : Print this message\n" );
    printf( "  --version,        -v     : Return the version/program info\n" );
    printf( "  --blocksize <n>,  -s<n>  : Set the block size and slice size to use [%lu]\n", param->blocksize );
    printf( "  --redundancy <n>, -r<n>  : Level of Redundancy (%%) [%.2f]\n", param->redundancy );
    printf( "  --threads <n>,    -t<n>  : Amount of threads to use [%d]\n", param->n_threads );
    printf( "  --memory <n>,     -m<n>  : Maximum amount of memory to be used for recovery blocks [%lu]\n", param->memory_max );
    printf( "\nIf you wish to create par2 files for a single source file, you may leave\n" );
    printf( "out the name of the par2 file from the command line. spar2 will then\n" );
    printf( "assume that you wish to base the filenames for the par2 files on the name\n" );
    printf( "of the source file.\n" );
}


void spar_file_writer( spar_t *h )
{
    progress_t *progress = h->threads[0].progress;

    for( int f = 0; f < h->n_recovery_files; f++ )
    {
        // Open the file for writing
        FILE *fp = fopen( h->recovery_filenames[f], "wb" );

        // Get the packets, and write them to the file
        for( int p = 0; p < h->packets_recvfile[f]; p++ )
        {
            pkt_header_t *packet = spar_get_packet_adv( h, f, p );

            if( packet == NULL )
            {
                printf( "File Writer: Error getting packet %d of file %d\n", p, f );
                continue;
            }

            fwrite( packet, 1, packet->length, fp );

            // If this was a recovery packet, update the status
            if( !memcmp( packet->type, PACKET_RECVSLIC, 16 ) )
            {
                // Update and print the progress
                pthread_mutex_lock( progress->mut );
                progress->w_done++;
                progress_print( progress );
                pthread_mutex_unlock( progress->mut );
            }

            free( packet );
        }
        fclose( fp );
    }

    // Write the empty .par2 with only the critical packets
    size_t fnlength = strlen(h->param.basename) + 6;
    char *filename = malloc( fnlength );
    snprintf( filename, fnlength, "%s.par2", h->param.basename );

    FILE *fp = fopen( filename, "wb" );

    // The FileDesc, IFSC and Main packets
    for ( int i = 0; i < h->n_critical_packets; i++ )
    {
        pkt_header_t *packet = h->critical_packets[i];
        fwrite( packet, 1, packet->length, fp );
    }

    // Write the Creator packet
    pkt_header_t *packet = &h->creator_packet->header;
    fwrite( packet, 1, packet->length, fp );

    fclose( fp );

    free( filename );
}

static char short_options[] = "hm:r:s:t:vz";
static struct option long_options[] =
{
    { "help",              no_argument, NULL, 'h' },
    { "version",           no_argument, NULL, 'v' },
    { "memory",      required_argument, NULL, 'm' },
    { "redundancy",  required_argument, NULL, 'r' },
    { "blocksize",   required_argument, NULL, 's' },
    { "threads",     required_argument, NULL, 't' },
    { "mimic",             no_argument, NULL, 'z' },
    {0, 0, 0, 0}
};

int spar_parse( spar_param_t *param, int argc, char **argv )
{
    // Commandline Parsing

    // Defaults
    spar_param_default( param );

    // First check for help
    if ( argc == 1 )
    {
        help( param );
        exit( 0 );
    }
    for( optind = 0;; )
    {
        int c = getopt_long( argc, argv, short_options, long_options, NULL );

        if( c == -1 )
            break;
        else if( c == 'h' )
        {
            help( param );
            exit(0);
        }
    }

    // Check for other options
    for( optind = 0;; )
    {
        int c = getopt_long( argc, argv, short_options, long_options, NULL );

        if( c == -1 )
            break;

        switch( c )
        {
            case 'v':
                spar_print_version();
                exit(0);
            case 'm':
                param->memory_max = atol( optarg );
                switch( optarg[strlen(optarg) - 1] )
                {
                    case 'K':
                    case 'k':
                        param->memory_max <<= 10;
                        break;
                    case 'M':
                    case 'm':
                        param->memory_max <<= 20;
                        break;
                    case 'G':
                    case 'g':
                        param->memory_max <<= 30;
                        break;
                    default:
                        break;
                }
                break;
            case 'r':
                param->redundancy = atof( optarg );
                break;
            case 's':
                param->blocksize = atoi( optarg );
                break;
            case 't':
                param->n_threads = atoi( optarg );
                break;
            case 'z':
                param->mimic = 1;
                break;
            default:
                printf( "Error: getopt returned character code 0%o = %c\n", c, c );
                return -1;
        }
    }

    // Skip the c(reate)
    optind++;

    if ( optind < argc )
    {
        int remaining = argc - optind;

        // Get the basename of the par2 files
        if ( remaining == 1 )
            param->basename = strdup( argv[optind] );
        else
        {
             char *ext = strstr( argv[optind], ".par2" );
            if (ext == NULL)
                param->basename = strdup( argv[optind] );
            else
            {
                // Copy the string until ".par2" into param->basename
                int basename_length = ext - argv[optind];
                param->basename = memcpy( calloc( basename_length + 1, 1 ), argv[optind], basename_length );
            }
            optind++;
        }

        param->n_input_files = argc - optind;
    }

    if ( param->n_threads < 1 )
        param->n_threads = 1;

    param->input_files = malloc( param->n_input_files * sizeof(spar_diskfile_t) );

    for ( int i = 0; i < param->n_input_files; i++ )
    {
        spar_diskfile_t *df = &param->input_files[i];

        df->filename = strdup( argv[optind++] );
        df->virtual_filename = NULL;
        df->offset   = 0;
        df->filesize = FILESIZE( df->filename );
    }

    param->param_free = &spar_param_free;

    return 0;
}

int main( int argc, char **argv )
{
    spar_param_t param;

    spar_param_default( &param );

    int ret = spar_parse( &param, argc, argv );

    if ( ret < 0 ) {
        printf( "Error parsing commandline arguments.\n" );
        exit(1);
    }

    spar_t *h = spar_generator_open( &param );

    spar_file_writer( h );

    spar_generator_close( h );
}
