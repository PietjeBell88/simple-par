#ifndef SPAR_PAR2_H
#define SPAR_PAR2_H

#include <stdint.h>

#include "spar2_version.h"
#include "spar2.h"
#include "common.h"

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
extern char PACKET_MAIN[];
extern char PACKET_FILEDESC[];
extern char PACKET_IFSC[];
extern char PACKET_RECVSLIC[];
extern char PACKET_CREATOR[];

extern char PAR2_MAGIC[];

extern char PAR2_CREATOR[];

extern char PAR2_PAR2CMDLINE[];

/************* PACKETS **************/
#pragma pack(1)

// Packet Header
struct pkt_header_t
{
    char     magic[8];
    uint64_t length;
    md5_t    packet_md5;
    md5_t    recovery_id;
    char     type[16];
};

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
    progress_t *progress;

    int block_start;
    int block_end;  // Inclusive
    pthread_t thread;
    uint16_t **recv_data;
} thread_t;

struct spar_t
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
};

void    spar_param_default( spar_param_t * );

spar_t * spar_generator_open( spar_param_t *);

pkt_header_t * spar_get_packet_adv( spar_t *, int, int );

void spar_generator_close( spar_t * );

void spar_param_default( spar_param_t *param );

void spar_param_free( void *arg );

#endif