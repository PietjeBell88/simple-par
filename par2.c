// turn the weird PACKET_TYPE stuff back into normal strings.. since we have a macro anyway

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>

#include "par2.h"
#include "diskfile.h"
#include "common.h"
#include "reedsolomon.h"

#include "extern/getopt.h"
#include "extern/crc32.h"
#include "extern/md5.h"

void generate_critical_packets( spar_t *h );

void md5_packet( pkt_header_t *header );
void md5_16k( diskfile_t *df );

void sort(md5_t arr[], int beg, int end);

void * rs_process_wrapper( void *thread );


static void spar_print_version()
{
    printf( "spar2 version: \"%s\"\n\n", SPAR_VERSION );
    printf( "It's like par2cmdline but with less features:\n" );
    printf( " - Only creation of par2 files. (no repair or verification)\n" );
    printf( " - Only limited size exponential scheme supported (-l in par2cmdline).\n" );
    printf( " - Only block size and redundancy can be set.\n\n" );
    printf( "Made possible by \"Bored at Work\"^TM.\n" );
}

static void help( spar_t *h )
{
    printf( "spar2 version: \"%s\"\n\n", SPAR_VERSION );
    printf( "Usage: spar2 c(reate) [options] <par2 file> [files]\n\n" );
    printf( "Options:\n" );
    printf( "  --help,           -h     : Print this message\n" );
    printf( "  --version,        -v     : Return the version/program info\n" );
    printf( "  --blocksize <n>,  -s<n>  : Set the block size and slice size to use [%lu]\n", h->blocksize );
    printf( "  --redundancy <n>, -r<n>  : Level of Redundancy (%%) [%.2f]\n", h->redundancy );
    printf( "  --threads <n>,    -t<n>  : Amount of threads to use [%d]\n", h->n_threads );
    printf( "  --memory <n>,     -m<n>  : Maximum amount of memory to be used for recovery blocks [%lu]\n", h->memory_max );
    printf( "\nIf you wish to create par2 files for a single source file, you may leave\n" );
    printf( "out the name of the par2 file from the command line. spar2 will then\n" );
    printf( "assume that you wish to base the filenames for the par2 files on the name\n" );
    printf( "of the source file.\n" );
}

void create_recovery_files( spar_t *h )
{
    // Progress of calculation and writing
    progress_t *progress = progress_init( h->n_recovery_blocks, h->n_input_slices );

    // Exponential scheme
    int blocks_current_file = 1;
    int blocknum = 0;

    // Allocate buffers for recovery blocks in memory
    int recv_blocks_mem = h->blocks_per_thread * h->n_threads;
    uint16_t **recv_data = malloc( recv_blocks_mem * sizeof(uint16_t*) );

    for ( int i = 0; i < recv_blocks_mem; i++ )
        recv_data[i] = memalign( 16, (h->blocksize + 15) & ~15 );

    // Threads
    thread_t *threads = malloc( h->n_threads * sizeof(thread_t) );

    for ( int i = 0; i < h->n_threads; i++ )
    {
        threads[i].h           = h;
        threads[i].block_start = i * h->blocks_per_thread;
        threads[i].block_end   = MIN(h->n_recovery_blocks - 1, threads[i].block_start + h->blocks_per_thread - 1);
        threads[i].recv_data   = &recv_data[threads[i].block_start];
        threads[i].progress    = progress;
        if ( threads[i].block_start <= threads[i].block_end )
            pthread_create( &threads[i].thread, NULL, rs_process_wrapper, &threads[i] );
    }

    // How many recovery files will we create?
    uint32_t whole = h->n_recovery_blocks / h->max_blocks_per_file;
    whole = (whole >= 1) ? whole-1 : 0;

    h->n_recovery_files = whole;
    int extra = h->n_recovery_blocks - whole * h->max_blocks_per_file;

    for ( int b = extra; b > 0; b >>= 1 )
        h->n_recovery_files++;

    // Allocate the recovery filenames
    h->recovery_filenames = malloc( h->n_recovery_files * sizeof(char*) );
    size_t fnlength = strlen(h->basename) + strlen(h->par2_fnformat) + 20;
    for ( int i = 0; i < h->n_recovery_files; i++ )
        h->recovery_filenames[i] = malloc( fnlength );

    // Useful alias
    pkt_header_t *header;

    // Generate the creator packet
    int creator_string_length = snprintf( 0, 0, "%s", PAR2_CREATOR );
    size_t packet_length = sizeof(pkt_creator_t) + ((creator_string_length + 3) & ~3);
    pkt_creator_t *creator = calloc( packet_length, 1 );
    header = &creator->header;
    SET_HEADER(header, h->recovery_id, PACKET_CREATOR, packet_length);
    SET_CREATOR((char*)(creator+1), creator_string_length);
    md5_packet( header );

    // Allocate the packets and data block contiguously
    packet_length = sizeof(pkt_recvslice_t) + h->blocksize;
    pkt_recvslice_t *recvslice = malloc( packet_length );

    int t = 0; // Thread index that has the next few blocks

    for ( int filenum = 0; filenum < h->n_recovery_files; filenum++ )
    {
        // Exponential at the bottom, full at the top
        if ( extra > 0 )
            blocks_current_file = MIN(blocks_current_file, extra);
        else
            blocks_current_file = h->max_blocks_per_file;

        // How many copies of each critical packet
        int copies_crit = 0;
        for ( int t = blocks_current_file; t > 0; t >>= 1)
            copies_crit++;

        // Open the file for writing
        char *filename = h->recovery_filenames[filenum];
        snprintf( filename, fnlength, h->par2_fnformat, h->basename, blocknum, blocks_current_file );
        FILE *fp = fopen( filename, "wb" );

        // Packet Header
        header = &recvslice->header;
        SET_HEADER(header, h->recovery_id, PACKET_RECVSLIC, packet_length);

        // Calculate the recovery slices
        for ( int i = 0, tx = 0, crit_i = 0; i < blocks_current_file; i++ )
        {
            // Wait for a thread to be done processing the next few blocks
            if ( blocknum == threads[t].block_start )
                pthread_join( threads[t].thread, NULL );

            recvslice->exponent = blocknum;
            int recv_index = blocknum - threads[t].block_start + t * h->blocks_per_thread;
            memcpy( (uint16_t*)(recvslice+1), recv_data[recv_index], h->blocksize );

            md5_packet( header );

            // TODO: valgrind error? But WHY~?
            fwrite( recvslice, 1, packet_length, fp );

            // Add some critical packets based on par2cmdline's algorithm
            tx += copies_crit * h->n_critical_packets;    // t*x += t*1
            while (tx >= blocks_current_file)             // while t*x > t*(n/t)
            {
                // Calculate the critical packets if they haven't been yet
                if ( h->critical_packets == NULL )
                    generate_critical_packets( h );

                pkt_header_t *packet = h->critical_packets[crit_i];
                fwrite( packet, 1, packet->length, fp );

                crit_i = (crit_i + 1) % h->n_critical_packets;

                tx -= blocks_current_file;                // t*x -= t*(n/t)
            }
            blocknum += 1;

            // spawn a new thread for some new blocks (if there are any)
            if ( blocknum > threads[t].block_end )
            {
                int last_thread_index =  (t + h->n_threads - 1) % h->n_threads;
                threads[t].block_start = threads[last_thread_index].block_end + 1;
                threads[t].block_end   = MIN(h->n_recovery_blocks - 1, threads[t].block_start + h->blocks_per_thread - 1);
                if ( threads[t].block_start <= threads[t].block_end )
                    pthread_create( &threads[t].thread, NULL, rs_process_wrapper, &threads[t] );

                t = (t+1) % h->n_threads;
            }


            // Update and print the progress
            pthread_mutex_lock( progress->mut );
            progress->w_done++;
            progress_print( progress );
            pthread_mutex_unlock( progress->mut );
        }

        // Write the creator packet
        header = (pkt_header_t*)creator;
        fwrite( header, 1, header->length, fp );

        fclose( fp );

        extra = extra - blocks_current_file;
        blocks_current_file = blocks_current_file << 1;
    }

    free( recvslice );

    // Write the empty .par2 with only the critical packets
    fnlength = strlen(h->basename) + 6;
    char *filename = malloc( fnlength );
    snprintf( filename, fnlength, "%s.par2", h->basename );

    FILE *fp = fopen( filename, "wb" );

    // The FileDesc, IFSC and Main packets
    for ( int i = 0; i < h->n_critical_packets; i++ )
    {
        pkt_header_t *packet = h->critical_packets[i];
        fwrite( packet, 1, packet->length, fp );
    }

    // The Creator packet
    header = (pkt_header_t*)creator;
    fwrite( header, 1, header->length, fp );
    fclose( fp );

    free( filename );
    free( creator );

    for ( int i = 0; i < recv_blocks_mem; i++ )
        free( recv_data[i] );
    free( recv_data );

    progress_delete( progress );
    free( progress );

    free( threads );

    // Print a newline so the process will stay on screen
    printf( "\n" );
}

static char short_options[] = "hm:r:s:t:v";
static struct option long_options[] =
{
    { "help",              no_argument, NULL, 'h' },
    { "version",           no_argument, NULL, 'v' },
    { "memory",      required_argument, NULL, 'm' },
    { "redundancy",  required_argument, NULL, 'r' },
    { "blocksize",   required_argument, NULL, 's' },
    { "threads",     required_argument, NULL, 't' },
    {0, 0, 0, 0}
};

int spar_parse( spar_t *h, int argc, char **argv )
{
    // Commandline Parsing

    // Defaults
    h->memory_max = 0;
    h->redundancy = 5;
    h->blocksize  = 640000;
    h->n_threads  = 1;

    // First check for help
    if ( argc == 1 )
    {
        help( h );
        exit( 0 );
    }
    for( optind = 0;; )
    {
        int c = getopt_long( argc, argv, short_options, long_options, NULL );

        if( c == -1 )
            break;
        else if( c == 'h' )
        {
            help( h );
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
                h->memory_max = atol( optarg );
                switch( optarg[strlen(optarg) - 1] )
                {
                    case 'K':
                    case 'k':
                        h->memory_max <<= 10;
                        break;
                    case 'M':
                    case 'm':
                        h->memory_max <<= 20;
                        break;
                    case 'G':
                    case 'g':
                        h->memory_max <<= 30;
                        break;
                    default:
                        break;
                }
                break;
            case 'r':
                h->redundancy = atoi( optarg );
                break;
            case 's':
                h->blocksize = atoi( optarg );
                break;
            case 't':
                h->n_threads = atoi( optarg );
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
            h->basename = strdup( argv[optind] );
        else
        {
             char *ext = strstr( argv[optind], ".par2" );
            if (ext == NULL)
                h->basename = strdup( argv[optind] );
            else
            {
                // Copy the string until ".par2" into h->basename
                int basename_length = ext - argv[optind];
                h->basename = memcpy( calloc( basename_length + 1, 1 ), argv[optind], basename_length );
            }
            optind++;
        }

        h->n_input_files = argc - optind;
    }

    if ( h->n_threads < 1 )
        h->n_threads = 1;

    unsigned char md5_filler[] = {0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42};

    SET_MD5(h->recovery_id, md5_filler); // Just a filler

    h->input_files = malloc( h->n_input_files * sizeof(diskfile_t) );

    h->n_input_slices = 0;
    h->largest_filesize = 0;

    for ( int i = 0; i < h->n_input_files; i++ )
    {
        diskfile_t *df = &h->input_files[i];

        df->filename = strdup( argv[optind++] );
        df->offset   = 0; //offsets[i];
        df->filesize = FILESIZE( df->filename ) - df->offset;
        df->n_slices = (df->filesize + h->blocksize - 1) / h->blocksize;

        md5_16k( df );
        SET_MD5(df->hash_full, md5_filler); // Just a filler
        df->checksums = malloc( df->n_slices * sizeof(checksum_t) );

        h->n_input_slices += df->n_slices;
        h->largest_filesize = MAX(h->largest_filesize, df->filesize);
    }

    h->n_recovery_blocks = (h->n_input_slices * h->redundancy + 50) / 100;
    if ( h->n_recovery_blocks == 0 && h->redundancy > 0 )
        h->n_recovery_blocks = 1;

    h->max_blocks_per_file = (h->largest_filesize + h->blocksize - 1) / h->blocksize;

    h->n_critical_packets = h->n_input_files * 2 + 1;  // n*(fdesc+ifsc) + main
    h->critical_packets = NULL;

    h->blocks_per_thread = (h->n_recovery_blocks + h->n_threads - 1) / h->n_threads;

    if ( h->memory_max > 0 )
        h->blocks_per_thread = MIN(h->blocks_per_thread, h->memory_max / h->blocksize / h->n_threads);

    if ( h->blocks_per_thread < 1 )
    {
        printf( "Input error: Maximum memory limit is smaller than one blocksize!\n" );
        printf( "Setting it to calculate one block per thread at a time.\n" );
        h->blocks_per_thread = 1;
    }

    // TODO: Overestimate, fix by generating all filenames and blocknumbers before calculations
    int digits_low   = snprintf( 0, 0, "%d", h->n_recovery_blocks );
    int digits_count = snprintf( 0, 0, "%d", MIN(h->max_blocks_per_file, h->n_recovery_blocks) );

    sprintf( h->par2_fnformat, "%%s.vol%%0%dd+%%0%dd.par2", digits_low, digits_count );

    return 0;
}

void set_recovery_id( spar_t *h )
{
    // Run through the critical packets once, to calculate the recovery id
    generate_critical_packets( h );

    // Delete these critical packets again, because most of their data is bogus
    for ( int i = 0; i < h->n_critical_packets; i++ )
        free( h->critical_packets[i] );
    free( h->critical_packets );
    h->critical_packets = NULL;
}

void generate_critical_packets( spar_t *h )
{
    md5_t *fids = malloc( h->n_input_files * sizeof(md5_t) );

    h->critical_packets = malloc( h->n_critical_packets * sizeof(pkt_header_t*) );
    int crit_i = 0;  // Index of current critical packet

    //****** FILE DESCRIPTION PACKETS ******//

    for ( int i = 0; i < h->n_input_files; i++ )
    {
        diskfile_t *df = &h->input_files[i];
        char *filename_in = df->filename;

        // Allocate a file descriptor packet
        size_t fn_length = (strlen(filename_in) + 3) & ~3;
        size_t packet_length = sizeof(pkt_filedesc_t) + fn_length;

        h->critical_packets[crit_i] = malloc( packet_length );
        pkt_filedesc_t *fdesc = (pkt_filedesc_t*)h->critical_packets[crit_i];
        memset( fdesc, 0, packet_length );
        crit_i++;

        // Aliases
        pkt_header_t *header  = &fdesc->header;

        // Copy the filename into the packet. Note that the filename does NOT have to be 0 terminated
        char *filename = (char *)(fdesc + 1);
        memset( filename, 0, fn_length );
        memcpy( filename, filename_in, strlen(filename_in) );

        // Write the packet info
        fdesc->flength = df->filesize;
        SET_MD5(fdesc->hash_full, df->hash_full);
        SET_MD5(fdesc->hash_16k,  df->hash_16k);

        // strlen(filename_in) instead of the entire field, to match par2cmdline
        md5_memory( (void*)&fdesc->hash_16k, sizeof(md5_t)+sizeof(uint64_t)+strlen(filename_in), &fdesc->fid );

        // Save the file id for the main packet
        SET_MD5(&(fids[i]), &fdesc->fid);

        SET_HEADER(header, h->recovery_id, PACKET_FILEDESC, packet_length);
        md5_packet( header );

        //****** INPUT FILE SLICE CHECKSUM PACKETS ******//

        packet_length = sizeof(pkt_ifsc_t) + h->input_files[i].n_slices * sizeof(checksum_t);
        h->critical_packets[crit_i] = malloc( packet_length );
        pkt_ifsc_t *ifsc  = (pkt_ifsc_t *)h->critical_packets[crit_i];
        crit_i++;

        // Alias
        header  = &ifsc->header;
        checksum_t *chksm = (checksum_t*)(ifsc + 1);

        // Known Header stuff
        SET_HEADER(header, h->recovery_id, PACKET_IFSC, packet_length);

        // Copy file id from the fdesc packet
        SET_MD5(&ifsc->fid, &fdesc->fid);

        // Calculate the checksums of all the slices
        for( int s = 0; s < h->input_files[i].n_slices; s++ )
            memcpy( &chksm[s], &df->checksums[s], sizeof(checksum_t) );

        md5_packet( header );
    }

    //****** MAIN PACKET ******//

    // Allocate the main packet
    size_t packet_length = sizeof(pkt_main_t) + h->n_input_files * sizeof(md5_t);

    h->critical_packets[crit_i] = malloc( packet_length );
    pkt_main_t *main_packet = (pkt_main_t*)h->critical_packets[crit_i];
    crit_i++;

    // Alias
    pkt_header_t *header = &main_packet->header;
    SET_HEADER(header, h->recovery_id, PACKET_MAIN, packet_length);

    // Fill in the main packet
    main_packet->slice_size = h->blocksize;
    main_packet->n_files    = h->n_input_files;
    sort( fids, 0, main_packet->n_files );
    memcpy( main_packet + 1, fids, h->n_input_files * sizeof(md5_t) );

    md5_packet( header );

    // Recovery ID as generated from the BODY of the main packet
    md5_memory( (void*)(header+1), packet_length - sizeof(pkt_header_t), &h->recovery_id );

    free( fids );
}

int main( int argc, char **argv )
{
    // blocksize, redundancy,
    // Initialize the Galois-Field tables
    setup_tables();

    spar_t h = {0};

    int ret = spar_parse( &h, argc, argv );

    if ( ret < 0 ) {
        printf( "Error parsing commandline arguments.\n" );
        exit(1);
    }

    set_recovery_id( &h );

    create_recovery_files( &h );

    //****** FREE MEMORY ******//
    for ( int i = 0; i < h.n_recovery_files; i++ )
        free( h.recovery_filenames[i] );
    free( h.recovery_filenames );

    free( h.basename );

    for( int i = 0; i < h.n_input_files; i++ )
    {
        free( h.input_files[i].filename );
        free( h.input_files[i].checksums );
    }

    free( h.input_files );

    for ( int i = 0; i < h.n_critical_packets; i++ )
        free( h.critical_packets[i] );
    free( h.critical_packets );
}


void md5_packet( pkt_header_t *header )
{
    md5_memory( (void*)(&header->recovery_id), header->length - 32, &(header->packet_md5) );
}

void md5_16k( diskfile_t *df )
{
    context_md5_t *ctx = malloc( sizeof(context_md5_t) );
    char buf[1L << 14];

    size_t read = read_to_buf( df, 0, 1<<14, buf );
    MD5Init( ctx );
    MD5Update( ctx, (unsigned char*)buf, read );
    MD5Final( (unsigned char*)df->hash_16k, ctx );

    free( ctx );
}

// To compare md5 digests
#ifndef WORDS_BIGENDIAN
#define HIGH 0
#define LOW 1
#else
#define HIGH 1
#define LOW 0
#endif
void swap(md5_t *a, md5_t *b)
{
    md5_t t;
    SET_MD5(&t,a);
    SET_MD5(a,b);
    SET_MD5(b,&t);
}

void sort(md5_t arr[], int beg, int end)
{
    if (end > beg + 1)
    {
        md5_t piv;
        SET_MD5(&piv,&arr[beg]);
        int l = beg + 1, r = end;
        while (l < r)
        {
            uint64_t* temp_arrl = (uint64_t*) &arr[l];
            uint64_t* temp_piv  = (uint64_t*) &piv;

            if (temp_arrl[HIGH] < temp_piv[HIGH] || (temp_arrl[HIGH] == temp_piv[HIGH] && temp_arrl[LOW] <= temp_piv[LOW]))
                l++;
            else
                swap(&arr[l], &arr[--r]);
        }
        swap(&arr[--l], &arr[beg]);
        sort(arr, beg, l);
        sort(arr, r, end);
    }
}

void * rs_process_wrapper( void *threadvoid )
{
    thread_t *thread = (thread_t*)threadvoid;
    rs_process( thread->h->input_files, thread->h->n_input_files, thread->block_start, thread->block_end, thread->h->blocksize, thread->recv_data, thread->progress );
    return NULL;
}

#undef HIGH
#undef LOW


