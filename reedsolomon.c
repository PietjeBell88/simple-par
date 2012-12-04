#define NW (1 << 16)
#define PRIM_POLY 0x1100B;

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "common.h"
#include "reedsolomon.h"
#include "diskfile.h"

#include "extern/md5.h"
#include "extern/crc32.h"

// Log/antilog tables
uint16_t gflog[NW], gfilog[NW], vander[(NW/2 + 1)];

// Lookup tables
ALIGNED_16( uint16_t ll[256][256] );
ALIGNED_16( uint16_t lh[256][256] );
ALIGNED_16( uint16_t hl[256][256] );
ALIGNED_16( uint16_t hh[256][256] );

uint16_t gfmult( uint16_t a, uint16_t b)
{
    uint32_t sum_log;
    if (a == 0 || b == 0) return 0;
    sum_log = gflog[a] + gflog[b];
    if (sum_log >= NW-1) sum_log -= NW-1;
    return gfilog[sum_log];
}

uint16_t gfpow(uint16_t base, uint16_t exponent)
{
  if (exponent == 0) return 1;
  if (base == 0) return 0;

  uint32_t sum = gflog[base] * exponent;

  sum = (sum >> 16) + (sum & (NW-1));
  if (sum >= (NW-1))
    return gfilog[sum-(NW-1)];
  else
    return gfilog[sum];
}

void setup_tables()
{
    int log = 0;
    unsigned b = 1, v = 0;
    int n = 1;

    vander[0] = 1;

    for ( ; log < NW-1; log++ ) {
        // Write the log table
        gflog[b] = (uint16_t)log;
        gfilog[log] = (uint16_t)b;
        // Add possible vanderpol constants
        b = b << 1;
        if (b & NW) b = b ^ PRIM_POLY;

        v = log+1;
        if (v%3 != 0 && v%5 != 0 && v%17 != 0 && v%257 != 0) {
            vander[n] = b;
            n += 1;
        }
    }

    // Lookup tables
    for ( int i = 0; i < 256; i++ )
        for ( int j = 0; j < 256; j++ )
        {
            ll[i][j] = gfmult(i   , j   );
            lh[i][j] = gfmult(i   , j<<8);
            hl[i][j] = gfmult(i<<8, j);
            hh[i][j] = gfmult(i<<8, j<<8);
        }
}


void lookup_multiply( uint16_t f, uint16_t * __restrict slice, uint16_t * __restrict dest, size_t length )
{
    uint16_t fl = (f >> 0) & 0xff;
    uint16_t fh = (f >> 8);

    // Combine the four multiplication tables into one
    ALIGNED_16( uint32_t LH[512] );

    for ( int i = 0; i < 256; i++ )
    {
        LH[i]     = ll[fl][i] ^ hl[fh][i];
        LH[i+256] = lh[fl][i] ^ hh[fh][i];
    }

    for ( int i = 0; i < (length>>1); i++ )
    {
        uint16_t s = slice[i];

        uint16_t sl = (s >> 0) & 0xff;
        uint16_t sh = (s >> 8);

        dest[i] ^= LH[sl] ^ LH[sh+256];
    }
}

void rs_process( diskfile_t *files, int n_files, int block_start, int block_end, size_t blocksize, uint16_t **dest, progress_t *progress )
{
    // Set the destination buffers to zero
    int n_blocks = block_end - block_start + 1;
    for ( int d = 0; d < n_blocks; d++ )
        memset( dest[d], 0, blocksize );

    // Allocate read buffer
    uint16_t *slice = spar2_malloc( (blocksize + 15) & ~15 );

    // Calculate the md5 of the input files as we're going along (16kB is already calculated when opening the files)
    context_md5_t *ctx_full = malloc( sizeof(context_md5_t) );

    int col = 1;

    for ( int i = 0; i < n_files; i++ )
    {
        MD5Init( ctx_full );

        for ( int j = 0; j < files[i].n_slices; j++ )
        {
            size_t read = read_to_buf( &files[i], j*blocksize, blocksize, (void*)slice );

            if ( block_start == 0 )
            {
                // Update the file's full hash with the data in the slice
                MD5Update( ctx_full, (unsigned char*)slice, read );

                // Calculate the checksums for this particular slice; include trailing zeros
                checksum_t *chksm = &files[i].checksums[j];
                md5_memory( slice, blocksize, chksm->md5 );
                chksm->crc = 0;
                crc32( slice, blocksize, &chksm->crc );

            }
            for ( int d = 0, b = block_start; b <= block_end; d++, b++ )
            {
                uint16_t current = gfpow( vander[col], b );
                lookup_multiply( current, slice, dest[d], blocksize );
            }

            // Update and print the progress
            pthread_mutex_lock( progress->mut );
            progress->c_done += block_end - block_start + 1;
            progress_print( progress );
            pthread_mutex_unlock( progress->mut );

            col += 1;
        }
        MD5Final( (unsigned char*)files[i].hash_full, ctx_full );
    }

    free( ctx_full );
    spar2_free( slice );
}
