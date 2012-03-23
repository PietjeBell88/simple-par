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

#include "extern/crc32.h"
#include "extern/md5.h"

void md5_memory( char *buf, size_t length, md5_t *digest );
void md5_packet( pkt_header_t *header );
void md5_file( FILE *fp, md5_t *digest );
void md5_16k( FILE *fp, md5_t *digest );
void md5_fid( md5_t *hash_16k, uint64_t size, char *filename, size_t fn_length, md5_t *digest );
void sort(md5_t arr[], int beg, int end);


void create_recovery_files( spar_t *h )
{
    // Progress string
    int digits = snprintf( 0, 0, "%d", h->n_recovery_blocks );
    char *progress_format = malloc( 30 );
    sprintf( progress_format, "Computing block: %%%dd/%%%dd\r", digits, digits );

    // Exponential scheme
    int blocks_current_file = 1;
    int blocknum = 0;

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
    pkt_creator_t creator;
    header = &creator.header;
    SET_HEADER(header, h->recovery_id, PACKET_CREATOR, sizeof(pkt_creator_t));
    SET_CREATOR(creator.creator);
    md5_packet( header );

    // Allocate the packets and data block contiguously
    size_t packet_length = sizeof(pkt_recvslice_t) + h->blocksize;
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
            // Print the progress
            printf( progress_format, blocknum, h->n_recovery_blocks );
            fflush( stdout );

            recvslice->exponent = blocknum;
            recoveryslice( h->input_files, h->n_input_files, blocknum, h->blocksize, (uint16_t*)(recvslice+1) );

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
        }

        // Write the creator packet
        header = (pkt_header_t*)&creator;
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
    header = (pkt_header_t*)&creator;
    fwrite( header, 1, header->length, fp );
    fclose( fp );

    free( filename );
    free( progress_format );
}

void spar_parse( spar_t *h, int argc, char **argv )
{
    // Commandline Parsing
    h->blocksize  = atoi( argv[1] );
    h->redundancy = atof( argv[2] );
    h->n_input_files = argc - 3;
    unsigned char blabla[] = {0x42,0x08,0x09,0x66,0x0b,0x1e,0xcd,0x26,0x6f,0xfc,0xd0,0xcd,0xe5,0x82,0x2e,0x0a};
    SET_MD5(&h->recovery_id, &blabla);

    h->input_files = malloc( h->n_input_files * sizeof(diskfile_t) );

    h->n_input_slices = 0;
    h->largest_filesize = 0;

    for ( int i = 0; i < h->n_input_files; i++ )
    {
        diskfile_t *df = &h->input_files[i];

        strcpy( df->filename, argv[i+3] );
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
    int digits_low = 1;
    for ( int b = h->n_recovery_blocks; b >= 10; b /= 10 )
        digits_low++;

    // TODO: Overestimate, fix by generating all filenames and blocknumbers before calculations
    int digits_count = 1;
    for ( int b = MIN(h->max_blocks_per_file, h->n_recovery_blocks); b >= 10; b /= 10 )
        digits_count++;

    sprintf( h->par2_fnformat, "%%s.vol%%0%dd+%%0%dd.par2", digits_low, digits_count );

    sprintf( h->basename, "test" );
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

    spar_parse( &h, argc, argv );

    generate_critical_packets( &h );

    create_recovery_files( &h );

    //****** FREE MEMORY ******//
    for ( int i = 0; i < h.n_recovery_files; i++ )
        free( h.recovery_filenames[i] );
    free( h.recovery_filenames );

    free( h.input_files );

    for ( int i = 0; i < h.n_critical_packets; i++ )
        free( h.critical_packets[i] );
    free( h.critical_packets );

    free_tables();
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


