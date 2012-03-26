// TODO:
// proper slice reading
// turn the weird PACKET_TYPE stuff back into normal strings.. since we have a macro anyway
// CHECK: empty par file, critical packet order
// ANSWER: fdesc van 1st input file, ifsc 1st input file, fdesc 2nd input file, ifsc 2nd..  <...> main packet

#include <stdio.h>
#include <stdlib.h>

#include "par2.h"
#include "diskfile.h"
#include "common.h"
#include "reedsolomon.h"

#include "extern/getopt.h"
#include "extern/crc32.h"
#include "extern/md5.h"

void md5_memory( char *buf, size_t length, md5_t *digest );
void md5_packet( pkt_header_t *header );
void md5_file( FILE *fp, md5_t *digest );
void md5_16k( FILE *fp, md5_t *digest );
void md5_fid( md5_t *hash_16k, uint64_t size, char *filename, size_t fn_length, md5_t *digest );
void sort(md5_t arr[], int beg, int end);


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
    printf( "  --redundancy <n>, -r<n>  : Level of Redundancy (%%) [%.2f]\n\n", h->redundancy );
    printf( "If you wish to create par2 files for a single source file, you may leave\n" );
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

    // Generate all blocks
    uint16_t **recv_data = malloc( h->n_recovery_blocks * sizeof(uint16_t*) );

    for ( int i = 0; i < h->n_recovery_blocks; i++ )
        recv_data[i] = calloc( h->blocksize, 1 );

    rs_process( h->input_files, h->n_input_files, 0, h->n_recovery_blocks - 1, h->blocksize, recv_data, progress );

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
            recvslice->exponent = blocknum;
            memcpy( (uint16_t*)(recvslice+1), recv_data[blocknum], h->blocksize );

            md5_packet( header );

            // TODO: valgrind error? But WHY~?
            fwrite( recvslice, 1, packet_length, fp );

            // Add some critical packets based on par2cmdline's algorithm
            tx += copies_crit * h->n_critical_packets;    // t*x += t*1
            while (tx >= blocks_current_file)             // while t*x > t*(n/t)
            {
                pkt_header_t *packet = h->critical_packets[crit_i];
                fwrite( packet, 1, packet->length, fp );

                crit_i = (crit_i + 1) % h->n_critical_packets;

                tx -= blocks_current_file;                // t*x -= t*(n/t)
            }
            blocknum += 1;

            // Update and print the progress
            progress->w_done++;
            progress_print( progress );
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

    for ( int i = 0; i < h->n_recovery_blocks; i++ )
        free( recv_data[i] );
    free( recv_data );

    free( progress );

    // Print a newline so the process will stay on screen
    printf( "\n" );
}

static char short_options[] = "hr:s:v";
static struct option long_options[] =
{
    { "help",              no_argument, NULL, 'h' },
    { "version",           no_argument, NULL, 'v' },
    { "redundancy",  required_argument, NULL, 'r' },
    { "blocksize",   required_argument, NULL, 's' },
    {0, 0, 0, 0}
};

int spar_parse( spar_t *h, int argc, char **argv )
{
    // Commandline Parsing

    // Defaults
    h->redundancy = 5;
    h->blocksize  = 640000;

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
            case 'r':
                h->redundancy = atoi( optarg );
                break;
            case 's':
                h->blocksize = atoi( optarg );
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

    unsigned char initial_recid[] = {0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42};
    SET_MD5(h->recovery_id, initial_recid);

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

        h->n_input_slices += df->n_slices;
        h->largest_filesize = MAX(h->largest_filesize, df->filesize);
    }

    h->n_recovery_blocks = (h->n_input_slices * h->redundancy + 50) / 100;
    if ( h->n_recovery_blocks == 0 && h->redundancy > 0 )
        h->n_recovery_blocks = 1;

    h->max_blocks_per_file = (h->largest_filesize + h->blocksize - 1) / h->blocksize;

    h->n_critical_packets = h->n_input_files * 2 + 1;  // n*(fdesc+ifsc) + main

    // TODO: Overestimate, fix by generating all filenames and blocknumbers before calculations
    int digits_low   = snprintf( 0, 0, "%d", h->n_recovery_blocks );
    int digits_count = snprintf( 0, 0, "%d", MIN(h->max_blocks_per_file, h->n_recovery_blocks) );

    sprintf( h->par2_fnformat, "%%s.vol%%0%dd+%%0%dd.par2", digits_low, digits_count );

    return 0;
}

void generate_critical_packets( spar_t *h )
{
    md5_t *fids = malloc( h->n_input_files * sizeof(md5_t) );

    h->critical_packets = malloc( h->n_critical_packets * sizeof(pkt_header_t*) );
    int crit_i = 0;  // Index of current critical packet

    //****** FILE DESCRIPTION PACKETS ******//
    // TODO: Calculate md5 as we're going along, as with IFSC. 16k might be skippible as it's fast.

    for ( int i = 0; i < h->n_input_files; i++ )
    {
        char *filename_in = h->input_files[i].filename;

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
        fdesc->flength   = FILESIZE( filename_in );
        FILE *fp = fopen( filename_in, "rb" );
        md5_file( fp, &fdesc->hash_full );
        fseek( fp, 0, 0 );
        md5_16k( fp, &fdesc->hash_16k );
        fclose( fp );

        // strlen(filename_in) instead of the entire field, to match par2cmdline
        md5_memory( (void*)&fdesc->hash_16k, sizeof(md5_t)+sizeof(uint64_t)+strlen(filename_in), &fdesc->fid );

        // Save the file id for the main packet
        SET_MD5(&(fids[i]), &fdesc->fid);

        SET_HEADER(header, h->recovery_id, PACKET_FILEDESC, packet_length);
        md5_packet( header );

        //****** INPUT FILE SLICE CHECKSUM PACKETS ******//
        // TODO: md5 checksum of a certain slice as soon as possible
        // after load into memory check if md5 of slice is already calculated
        // Might help to slow down fastest thread

        // Allocate buffer
        char *slice = malloc( h->blocksize );

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
        {
            read_to_buf( &h->input_files[i], s * h->blocksize, h->blocksize, slice );
            md5_memory( slice, h->blocksize, &chksm[s].md5 );
            chksm[s].crc = 0;
            crc32( slice, h->blocksize, &chksm[s].crc );
        }

        md5_packet( header );

        free( slice );
    }

    //****** MAIN PACKET ******//
    // TODO: md5 checksum of first 16k as soon as possible and save it in memory so we can do par2 on the fly
    // TODO: file size should be known beforehand
    // TODO: file name should be known beforehand

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
    main_packet->n_files    = h->n_input_files; //TODO: Check with large file if definitely not recovery files?
    sort( fids, 0, main_packet->n_files );
    memcpy( main_packet + 1, fids, h->n_input_files * sizeof(md5_t) );

    md5_packet( header );

    // Recovery ID as generated from the BODY of the main packet
    md5_memory( (void*)(header+1), packet_length - sizeof(pkt_header_t), &h->recovery_id );

    // Write the recovery_id to the critical packets
    for( int i = 0; i < h->n_critical_packets; i++ )
    {
        SET_MD5(h->critical_packets[i]->recovery_id, h->recovery_id);
        md5_packet( h->critical_packets[i] );
    }

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

    generate_critical_packets( &h );

    create_recovery_files( &h );

    //****** FREE MEMORY ******//
    for ( int i = 0; i < h.n_recovery_files; i++ )
        free( h.recovery_filenames[i] );
    free( h.recovery_filenames );

    free( h.basename );

    for( int i = 0; i < h.n_input_files; i++ )
        free( h.input_files[i].filename );

    free( h.input_files );

    for ( int i = 0; i < h.n_critical_packets; i++ )
        free( h.critical_packets[i] );
    free( h.critical_packets );
}

void md5_memory( char *buf, size_t length, md5_t *digest )
{
    context_md5_t *ctx = malloc( sizeof(context_md5_t) );
    MD5Init( ctx );
    MD5Update( ctx, (unsigned char*) buf, length );
    MD5Final( (unsigned char*)digest, ctx );
    free( ctx );
}

void md5_packet( pkt_header_t *header )
{
    md5_memory( (void*)(&header->recovery_id), header->length - 32, &(header->packet_md5) );
}

void md5_file( FILE *fp, md5_t *digest )
{
    context_md5_t *ctx = malloc( sizeof(context_md5_t) );
    char buf[1L << 15];

    MD5Init( ctx );
    while( !feof( fp ) && !ferror( fp ) )
        MD5Update( ctx, (unsigned char*)buf, fread( buf, 1, sizeof(buf), fp ) );
    MD5Final( (unsigned char*)digest, ctx );

    free( ctx );
}

void md5_16k( FILE *fp, md5_t *digest )
{
    context_md5_t *ctx = malloc( sizeof(context_md5_t) );
    char buf[1L << 14];

    MD5Init( ctx );
    MD5Update( ctx, (unsigned char*)buf, fread( buf, 1, sizeof(buf), fp ) );
    MD5Final( (unsigned char*)digest, ctx );

    free( ctx );
}

void md5_fid( md5_t *hash_16k, uint64_t size, char *filename, size_t fn_length, md5_t *digest )
{
    context_md5_t *ctx = malloc( sizeof(context_md5_t) );
    size_t bufsize = sizeof( md5_t ) + sizeof( uint64_t ) + fn_length;
    char *buf = malloc( bufsize );
    char *temp = buf;

    // Copy into the buffer
    memcpy( temp, hash_16k, sizeof(md5_t) );
    temp += sizeof(md5_t);
    memcpy( temp, &size, sizeof(uint64_t) );
    temp += sizeof(uint64_t);
    memcpy( temp, filename, fn_length );

    MD5Init( ctx );
    MD5Update( ctx, (unsigned char*)buf, bufsize );
    MD5Final( (unsigned char*)digest, ctx );

    free( buf );
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
#undef HIGH
#undef LOW


