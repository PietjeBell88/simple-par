#ifndef SPAR_SPAR2_H
#define SPAR_SPAR2_H

#include <stdint.h>

#include "spar2_version.h"

typedef struct spar_diskfile_t spar_diskfile_t;
typedef struct spar_t spar_t;
typedef struct pkt_header_t pkt_header_t;

typedef struct
{
    // Input Files
    spar_diskfile_t *input_files;

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