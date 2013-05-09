#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <getopt.h>

#include "par2.h"
#include "diskfile.h"
#include "common.h"
#include "reedsolomon.h"

#include "extern/crc32.h"
#include "extern/md5.h"

// Packet strings
char PACKET_MAIN[]     = "PAR 2.0\0Main\0\0\0\0";
char PACKET_FILEDESC[] = "PAR 2.0\0FileDesc";
char PACKET_IFSC[]     = "PAR 2.0\0IFSC\0\0\0\0";
char PACKET_RECVSLIC[] = "PAR 2.0\0RecvSlic";
char PACKET_CREATOR[]  = "PAR 2.0\0Creator\0";

char PAR2_MAGIC[]   = "PAR2\0PKT";

char PAR2_CREATOR[] = "Created by Simple Par (spar2) revision \"" SPAR_VERSION "\"\0";

char PAR2_PAR2CMDLINE[] = "Created by par2cmdline version 0.4.\0";

// Forward declarations
void set_recovery_id( spar_t *h );
void generate_creator_packet( spar_t *h );
void generate_critical_packets( spar_t *h );

void md5_packet( pkt_header_t *header );
void md5_16k( spar_diskfile_t *df );

void sort(md5_t arr[], int beg, int end);

// Wrapper for arguments to send with pthread_create
typedef struct
{
    spar_t *h;
    thread_t *thread;
} wrapped_thread_t;

void * rs_process_wrapper( void *thread );

pkt_header_t * spar_get_packet_adv( spar_t *h, int filenum, int packet_index )
{
    const int recvfile_block_start = h->recvfile_block_start[filenum];
    const int blocks_current_file = h->recvfile_block_end[filenum] - h->recvfile_block_start[filenum] + 1;
    const int packets_recv_crit = h->packets_recvfile[filenum] - 1; // Exclude the creator packet in the calculations

    if( packet_index >= h->packets_recvfile[filenum] )
    {
        printf( "Error! No such packet: requested packet %d out of %d\n", packet_index, h->packets_recvfile[filenum] );
        return NULL;
    }

    // We always want to return a copy of the packet requested
    pkt_header_t *return_copy;
    size_t packet_length;

    // The last packet in each file is the creator packet
    if( packet_index == h->packets_recvfile[filenum] - 1 )
    {
        packet_length = h->creator_packet->header.length;
        return_copy = malloc( packet_length );
        memcpy( return_copy, h->creator_packet, packet_length );
        return return_copy;
    }

    // These two are not actual recovery block indices, although they both have the same offset.
    const int next_recv_block = ((packet_index + 1) * blocks_current_file - 1) / packets_recv_crit;
    const int cur_recv_block  = ((packet_index    ) * blocks_current_file - 1) / packets_recv_crit;

    // Check if we have to return a recovery packet (already a copy)
    if( packet_index == 0 || next_recv_block > cur_recv_block )
        return spar_recvslice_get( h, next_recv_block + recvfile_block_start );

    // Else we return a critical packet

    // Calculate the critical packets if they haven't been yet
    if ( h->critical_packets == NULL )
        generate_critical_packets( h );

    const int copies_crit = (packets_recv_crit - blocks_current_file) / h->n_critical_packets;

    int crit_packet_index = ((packet_index * copies_crit * h->n_critical_packets - 1) / packets_recv_crit)  % h->n_critical_packets;

    pkt_header_t *header = h->critical_packets[crit_packet_index];
    packet_length = header->length;

    return_copy = malloc( packet_length );
    memcpy( return_copy, header, packet_length );

    return return_copy;
}

int spar_get_packet( spar_t *h, spar_packet_t **packet )
{
    // Thread-safe ordered packet generator
    pthread_mutex_lock( h->generator_mut );

    // Check if we are not already done
    if( h->cur_filenum >= h->n_recovery_files )
    {
        pthread_mutex_unlock( h->generator_mut );
        return -1;
    }

    // If we have a packet to generate, get one:
    pkt_header_t *header = spar_get_packet_adv( h, h->cur_filenum, h->cur_packet );

    // Allocate a return packet
    *packet = malloc( sizeof(spar_packet_t) );

    (*packet)->filenum   = h->cur_filenum;
    (*packet)->packetnum = h->cur_packet;
    (*packet)->offset = h->cur_offset;
    (*packet)->size = header->length;
    (*packet)->data = (char*)header;

    // Set the file number and packet index of the next packet to be generated
    h->cur_packet += 1;
    h->cur_offset += header->length;

    if( h->cur_packet >= h->packets_recvfile[h->cur_filenum] )
    {
        h->cur_filenum += 1;
        h->cur_packet = 0;
        h->cur_offset = 0;
    }

    pthread_mutex_unlock( h->generator_mut );

    return 0;
}

pkt_header_t * spar_recvslice_get( spar_t *h, int blocknum )
{
    if ( blocknum >= h->n_recovery_blocks )
    {
        printf( "Error! Block %d requested, when there's only %d available.\n", blocknum, h->n_recovery_blocks );
        return NULL;
    }

    size_t packet_length = sizeof(pkt_recvslice_t) + h->param.blocksize;
    pkt_recvslice_t *recvslice = malloc( packet_length );

    // Useful alias
    pkt_header_t *header;

    // Calculate the thread index that has the block with block number blocknum
    int t = (blocknum / h->blocks_per_thread) % h->param.n_threads;

    // Packet Header
    header = &recvslice->header;
    SET_HEADER(header, h->recovery_id, PACKET_RECVSLIC, packet_length);

    // Wait for the thread to be done processing the requested blocks
    pthread_join( h->threads[t].thread, NULL );

    // Copy calculated data to the recovery slice
    recvslice->exponent = blocknum;
    int recv_index = blocknum - h->threads[t].block_start;
    memcpy( (uint16_t*)(recvslice+1), h->threads[t].recv_data[recv_index], h->param.blocksize );

    md5_packet( header );

    // spawn a new thread for some new blocks (if there are any)
    if ( blocknum == h->threads[t].block_end )
    {
        int last_thread_index =  (t + h->param.n_threads - 1) % h->param.n_threads;
        h->threads[t].block_start = h->threads[last_thread_index].block_end + 1;
        h->threads[t].block_end   = MIN(h->n_recovery_blocks - 1, h->threads[t].block_start + h->blocks_per_thread - 1);

        if ( h->threads[t].block_start <= h->threads[t].block_end )
        {
            wrapped_thread_t *wrap = malloc( sizeof(wrapped_thread_t) );
            wrap->h = h;
            wrap->thread = &h->threads[t];

            // The wrapper function free()s wrap
            pthread_create( &h->threads[t].thread, NULL, rs_process_wrapper, wrap );
        }
    }

    return header;
}

void filenames_and_packet_numbers( spar_t *h )
{
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

    // Allocate an array for the amount of packets and blocks in each recovery file
    h->recvfile_block_start = malloc( h->n_recovery_files * sizeof(int) );
    h->recvfile_block_end   = malloc( h->n_recovery_files * sizeof(int) );
    h->packets_recvfile     = malloc( h->n_recovery_files * sizeof(int) );

    // Allocate the recovery filenames
    h->recovery_filenames = malloc( h->n_recovery_files * sizeof(char*) );
    size_t fnlength = strlen(h->param.basename) + strlen(h->par2_fnformat) + 20;
    for ( int i = 0; i < h->n_recovery_files; i++ )
        h->recovery_filenames[i] = malloc( fnlength );

    for ( int filenum = 0; filenum < h->n_recovery_files; filenum++ )
    {
        // Exponential at the bottom, full at the top
        if ( extra > 0 )
            blocks_current_file = MIN(blocks_current_file, extra);
        else
            blocks_current_file = h->max_blocks_per_file;

        h->recvfile_block_start[filenum] = blocknum;
        h->recvfile_block_end[filenum] = blocknum + blocks_current_file - 1; // Inclusive

        // How many copies of each critical packet
        int copies_crit = 0;
        for ( int z = blocks_current_file; z > 0; z >>= 1)
            copies_crit++;

        // Total amount of packets in a recovery file is: recovery packets + critical packets + a single creator packet
        h->packets_recvfile[filenum] = blocks_current_file + copies_crit * h->n_critical_packets + 1;

        // Generate the filename for this recovery file
        char *filename = h->recovery_filenames[filenum];
        snprintf( filename, fnlength, h->par2_fnformat, h->param.basename, blocknum, blocks_current_file );

        blocknum += blocks_current_file;
        extra = extra - blocks_current_file;
        blocks_current_file = blocks_current_file << 1;
    }
}


spar_t * spar_generator_open( spar_param_t *param )
{
    setup_tables();

    spar_t *h = malloc( sizeof(spar_t) );

    memcpy( &h->param, param, sizeof(spar_param_t) );

    unsigned char md5_filler[] = {0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42};

    SET_MD5(h->recovery_id, md5_filler); // Just a filler

    h->n_input_slices = 0;
    h->largest_filesize = 0;

    for ( int i = 0; i < h->param.n_input_files; i++ )
    {
        spar_diskfile_t *df = &h->param.input_files[i];

        df->n_slices = (df->filesize + h->param.blocksize - 1) / h->param.blocksize;

        md5_16k( df );
        SET_MD5(df->hash_full, md5_filler); // Just a filler
        df->checksums = malloc( df->n_slices * sizeof(checksum_t) );

        h->n_input_slices += df->n_slices;
        h->largest_filesize = MAX(h->largest_filesize, df->filesize);
    }

    h->n_recovery_blocks = (uint16_t)(h->n_input_slices * h->param.redundancy + 50) / 100;
    if ( h->n_recovery_blocks == 0 && h->param.redundancy > 0.f )
        h->n_recovery_blocks = 1;

    h->max_blocks_per_file = (h->largest_filesize + h->param.blocksize - 1) / h->param.blocksize;

    h->n_critical_packets = h->param.n_input_files * 2 + 1;  // n*(fdesc+ifsc) + main
    h->critical_packets = NULL;

    h->blocks_per_thread = (h->n_recovery_blocks + h->param.n_threads - 1) / h->param.n_threads;

    if ( h->param.memory_max > 0 )
        h->blocks_per_thread = MIN(h->blocks_per_thread, param->memory_max / param->blocksize / param->n_threads);

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

    //
    set_recovery_id( h );

    filenames_and_packet_numbers( h );

    generate_creator_packet( h );


    // Progress of calculation and writing
    progress_t *progress = progress_init( h->n_recovery_blocks, h->n_input_slices );

    // Threads
    h->threads = malloc( h->param.n_threads * sizeof(thread_t) );

    for ( int i = 0; i < h->param.n_threads; i++ )
    {
        uint16_t **recv_data = malloc( h->blocks_per_thread * sizeof(uint16_t*) );
        for ( int b = 0; b < h->blocks_per_thread; b++ )
            recv_data[b] = spar2_malloc( (h->param.blocksize + 15) & ~15 );

        h->threads[i].block_start = i * h->blocks_per_thread;
        h->threads[i].block_end   = MIN(h->n_recovery_blocks - 1, h->threads[i].block_start + h->blocks_per_thread - 1);
        h->threads[i].recv_data   = recv_data;
        h->threads[i].progress    = progress;

        if ( h->threads[i].block_start <= h->threads[i].block_end )
        {
            wrapped_thread_t *wrap = malloc( sizeof(wrapped_thread_t) );
            wrap->h = h;
            wrap->thread = &h->threads[i];

            // The wrapper function free()s wrap
            pthread_create( &h->threads[i].thread, NULL, rs_process_wrapper, wrap );
        }
    }

    // Thread-safe packet generator
    h->generator_mut = malloc( sizeof(pthread_mutex_t) );
    pthread_mutex_init( h->generator_mut, NULL );

    h->cur_filenum = 0;
    h->cur_packet = 0;

    return h;
}

void spar_get_filenames_packets( spar_t *h, char ***filenames, int **n_packets, int *n_files )
{
    *n_files = h->n_recovery_files;

    *filenames = malloc( *n_files * sizeof(char *) );
    *n_packets = malloc( *n_files * sizeof(int) );

    for( int f = 0; f < h->n_recovery_files; f++ )
    {
        (*filenames)[f] = strdup( h->recovery_filenames[f] );
        (*n_packets)[f] = h->packets_recvfile[f];
    }
}

void spar_generator_close( spar_t *h )
{
    if ( h->param.param_free )
        h->param.param_free( &h->param );

    // Free buffers for the recovery blocks and threads
    for( int i = 0; i < h->param.n_threads; i++ )
    {
        for( int b = 0; b < h->blocks_per_thread; b++ )
            free( h->threads[i].recv_data[b] );
        free( h->threads[i].recv_data );
    }

    progress_delete( h->threads[0].progress );
    free( h->threads[0].progress );

    free( h->threads );

    for ( int i = 0; i < h->n_recovery_files; i++ )
        free( h->recovery_filenames[i] );
    free( h->recovery_filenames );

    for ( int i = 0; i < h->n_critical_packets; i++ )
        free( h->critical_packets[i] );
    free( h->critical_packets );

    free( h->creator_packet );

    free( h->recvfile_block_start );
    free( h->recvfile_block_end );
    free( h->packets_recvfile );

    pthread_mutex_destroy( h->generator_mut );
    free( h->generator_mut );

    free( h );
}

void spar_param_free( void *arg )
{
    spar_param_t *param = (spar_param_t*)arg;

    free( param->basename );

    for( int i = 0; i < param->n_input_files; i++ )
    {
        if( param->input_files[i].virtual_filename != NULL &&
            param->input_files[i].virtual_filename != param->input_files[i].filename )
            free( param->input_files[i].virtual_filename );

        free( param->input_files[i].filename );

        free( param->input_files[i].checksums );
    }

    free( param->input_files );
}

void spar_param_default( spar_param_t *param )
{
    param->memory_max = 0;
    param->redundancy = 5.f;
    param->blocksize  = 640000;
    param->n_threads  = 1;
    param->mimic      = 0;

    param->n_input_files = 0;
    param->input_files = NULL;

    param->param_free = NULL;
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

void generate_creator_packet( spar_t *h )
{
    // The length of the creator string used depends on the whether or not we want to imitate par2cmdline
    int creator_string_length;
    if ( h->param.mimic )
        creator_string_length = snprintf( 0, 0, "%s", PAR2_PAR2CMDLINE );
    else
        creator_string_length = snprintf( 0, 0, "%s", PAR2_CREATOR );

    size_t packet_length = sizeof(pkt_creator_t) + ((creator_string_length + 3) & ~3);

    h->creator_packet = calloc( packet_length, 1 );
    pkt_header_t *header = &h->creator_packet->header;
    SET_HEADER(header, h->recovery_id, PACKET_CREATOR, packet_length);

    // Choose between the two strings (see above)
    if ( h->param.mimic )
        SET_CREATOR((char*)(h->creator_packet+1), PAR2_PAR2CMDLINE, creator_string_length);
    else
        SET_CREATOR((char*)(h->creator_packet+1), PAR2_CREATOR, creator_string_length);

    md5_packet( header );
}


void generate_critical_packets( spar_t *h )
{
    md5_t *fids = malloc( h->param.n_input_files * sizeof(md5_t) );

    h->critical_packets = malloc( h->n_critical_packets * sizeof(pkt_header_t*) );
    int crit_i = 0;  // Index of current critical packet

    //****** FILE DESCRIPTION PACKETS ******//

    for ( int i = 0; i < h->param.n_input_files; i++ )
    {
        spar_diskfile_t *df = &h->param.input_files[i];
        char *filename_in = (df->virtual_filename == NULL) ? df->filename : df->virtual_filename;

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

        packet_length = sizeof(pkt_ifsc_t) + h->param.input_files[i].n_slices * sizeof(checksum_t);
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
        for( int s = 0; s < h->param.input_files[i].n_slices; s++ )
            memcpy( &chksm[s], &df->checksums[s], sizeof(checksum_t) );

        md5_packet( header );
    }

    //****** MAIN PACKET ******//

    // Allocate the main packet
    size_t packet_length = sizeof(pkt_main_t) + h->param.n_input_files * sizeof(md5_t);

    h->critical_packets[crit_i] = malloc( packet_length );
    pkt_main_t *main_packet = (pkt_main_t*)h->critical_packets[crit_i];
    crit_i++;

    // Alias
    pkt_header_t *header = &main_packet->header;
    SET_HEADER(header, h->recovery_id, PACKET_MAIN, packet_length);

    // Fill in the main packet
    main_packet->slice_size = h->param.blocksize;
    main_packet->n_files    = h->param.n_input_files;
    sort( fids, 0, main_packet->n_files );
    memcpy( main_packet + 1, fids, h->param.n_input_files * sizeof(md5_t) );

    md5_packet( header );

    // Recovery ID as generated from the BODY of the main packet
    md5_memory( (void*)(header+1), packet_length - sizeof(pkt_header_t), &h->recovery_id );

    free( fids );
}

void md5_packet( pkt_header_t *header )
{
    md5_memory( (void*)(&header->recovery_id), header->length - 32, &(header->packet_md5) );
}

void md5_16k( spar_diskfile_t *df )
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
    wrapped_thread_t *wrap = (wrapped_thread_t*)threadvoid;
    rs_process( wrap->h->param.input_files, wrap->h->param.n_input_files, wrap->thread->block_start, wrap->thread->block_end, wrap->h->param.blocksize, wrap->thread->recv_data, wrap->thread->progress );
    free( wrap );
    return NULL;
}

#undef HIGH
#undef LOW


