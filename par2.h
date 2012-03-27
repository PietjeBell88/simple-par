#ifndef SPAR_PAR2_H
#define SPAR_PAR2_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "spar2_version.h"

#include "diskfile.h"
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

#define SET_CREATOR(a,n) ({ \
for ( int _i = 0; _i < (n); _i++ ) \
    (a)[_i] = PAR2_CREATOR[_i]; \
})



char PACKET_MAIN[]     = {'P','A','R',' ','2','.','0','\0','M','a','i','n','\0','\0','\0','\0'};
char PACKET_FILEDESC[] = {'P','A','R',' ','2','.','0','\0','F','i','l','e','D','e','s','c'};
char PACKET_IFSC[]     = {'P','A','R',' ','2','.','0','\0','I','F','S','C','\0','\0','\0','\0'};
char PACKET_RECVSLIC[] = {'P','A','R',' ','2','.','0','\0','R','e','c','v','S','l','i','c'};
char PACKET_CREATOR[]  = {'P','A','R',' ','2','.','0','\0','C','r','e','a','t','o','r','\0'};

char PAR2_MAGIC[]   = {'P','A','R','2','\0','P','K','T'};

char PAR2_CREATOR[] = "Created by Simple Par (spar2) revision \"" SPAR_VERSION "\"\0";

typedef unsigned char md5_t[16];

typedef uint32_t crc32_t;

#pragma pack(1)

typedef struct {
    md5_t md5;
    crc32_t crc;
} checksum_t;


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
    int n_input_files;
    uint16_t n_input_slices;
    size_t largest_filesize;

    // Program Options
    float redundancy;
    uint64_t blocksize;
    md5_t recovery_id;
    uint16_t n_recovery_blocks;
    uint16_t max_blocks_per_file;

    int n_threads;
    int blocks_per_thread;
    size_t memory_max;

    // Par2 File Output
    char par2_fnformat[300];
    char *basename;
    int n_recovery_files;
    char **recovery_filenames;

    int n_critical_packets;
    pkt_header_t **critical_packets;  // Array of pointers to the critical packets
} spar_t;

typedef struct
{
    spar_t *h;
    int block_start;
    int block_end;  // Inclusive
    pthread_t thread;
    uint16_t **recv_data;
    progress_t *progress;
} thread_t;

#endif