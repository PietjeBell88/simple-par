#ifndef SPAR_PAR2_H
#define SPAR_PAR2_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "spar2_version.h"

#include "diskfile.h"

#define SET_MD5(a,b) ({ \
for ( int _i = 0; _i < 16; _i++ ) \
    ((unsigned char*)(a))[_i] = ((unsigned char*)(b))[_i]; \
})

#define SET_HEADER(hdr, recid, ptype, plength) ({ \
for ( int _i = 0; _i < 8; _i++ ) \
    hdr->magic[_i] = PAR2_MAGIC[_i]; \
\
for ( int _i = 0; _i < 16; _i++ ) \
    hdr->type[_i] = ptype[_i]; \
\
for ( int _i = 0; _i < 16; _i++ ) \
    hdr->packet_md5[_i] = 0; \
\
SET_MD5(hdr->recovery_id, recid); \
\
hdr->length = plength; \
})

#define SET_CREATOR(a,s,n) ({ \
for ( int _i = 0; _i < (n); _i++ ) \
    (a)[_i] = s[_i]; \
})


/********** PACKET STRINGS **********/
char PACKET_MAIN[]     = "PAR 2.0\0Main\0\0\0\0";
char PACKET_FILEDESC[] = "PAR 2.0\0FileDesc";
char PACKET_IFSC[]     = "PAR 2.0\0IFSC\0\0\0\0";
char PACKET_RECVSLIC[] = "PAR 2.0\0RecvSlic";
char PACKET_CREATOR[]  = "PAR 2.0\0Creator\0";

char PAR2_MAGIC[]   = "PAR2\0PKT";

char PAR2_CREATOR[] = "Created by Simple Par (spar2) revision \"" SPAR_VERSION "\"\0";

char PAR2_PAR2CMDLINE[] = "Created by par2cmdline version 0.4.\0";

/************* PACKETS **************/
#pragma pack(1)

// Packet Header
typedef struct {
    char     magic[8];
    uint64_t length;
    md5_t    packet_md5;
    md5_t    recovery_id;
    char     type[16];
} pkt_header_t;

// Main Packet
typedef struct
{
    pkt_header_t header;
    uint64_t slice_size;
    uint32_t n_files;
    //md5_t *fid_recovery;
    //md5_t *fid_non_recovery;
} pkt_main_t;

// File Description Packet
typedef struct
{
    pkt_header_t header;
    md5_t fid;
    md5_t hash_full;
    md5_t hash_16k;
    uint64_t flength;
    //char *fname;       // ?*4 bytes long
} pkt_filedesc_t;

// Input File Slice Checksum packet
typedef struct
{
    pkt_header_t header;
    md5_t fid;
    //checksum_t *slice_checksums;
} pkt_ifsc_t;

// Recovery Slice Packet
typedef struct
{
    pkt_header_t header;
    uint32_t exponent;
    //char *data;         // ?*4 bytes long

} pkt_recvslice_t;
// Creator Packet
typedef struct
{
    pkt_header_t header;
    //char creator[];  // ?*4 bytes long
} pkt_creator_t;

#pragma pack()

typedef struct
{
    // Input Files
    diskfile_t *input_files;

    // Program Options
    float redundancy;
    uint64_t blocksize;
    int n_threads;
    size_t memory_max;

    int n_input_files;
    char *basename;

    // Par2 File Output
    int mimic; // Mimic par2cmdline's creator packet?

    // free() callback
    void (*param_free)( void * );
} spar_param_t;

typedef struct
{
    progress_t *progress;

    int block_start;
    int block_end;  // Inclusive
    pthread_t thread;
    uint16_t **recv_data;
} thread_t;

typedef struct
{
    spar_param_t param;

    // Input Files
    //int n_input_files;
    uint16_t n_input_slices;
    size_t largest_filesize;

    // Program Options
    md5_t recovery_id;
    uint16_t n_recovery_blocks;
    uint16_t max_blocks_per_file;

    int blocks_per_thread;

    // Par2 File Output
    char par2_fnformat[300];
    int n_recovery_files;
    char **recovery_filenames;

    int *recvfile_block_start;
    int *recvfile_block_end;
    int *packets_recvfile;

    int n_critical_packets;
    pkt_header_t **critical_packets;  // Array of pointers to the critical packets
    pkt_creator_t *creator_packet;

    // Threads
    thread_t *threads;
} spar_t;


/* spar_param_default:
 *      fill x264_param_t with default values */
void    spar_param_default( spar_param_t * );


/* spar_generator_open:
 *      Returns a generator handler. Copies all parameters from spar2_param_t. */
spar_t * spar_generator_open( spar_param_t *);


/* spar_get_packet:
 *      Returns the packet with packet_index. This packet is either a critical
 *      packet (Main, IFSC, FileDesc), Creator packet, or Recovery packet.
 *      Although these packets could theoretically be requested out of order,
 *      the limitations of the spar2_recvslive_get function may result in bad
 *      behavior. */
pkt_header_t * spar_get_packet( spar_t *, int, int );

/* spar_generator_close:
 *      Closes the generator handle. */
void spar_generator_close( spar_t * );


/* spar_file_writer:
 *      Requests the par2 packets in order, and writes them to their respective
 *      recovery files.*/
void spar_file_writer( spar_t * );

/* spar_recvslice_get:
 *      Returns the recovery slice with block number blocknum.
 *      If the recovery packets are not requested in order, the function may
 *      return the wrong block. */
pkt_header_t * spar_recvslice_get( spar_t *, int );
#endif