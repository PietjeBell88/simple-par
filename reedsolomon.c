#define NW (1 << 16)
#define PRIM_POLY 0x1100B;

#define HAVE_SSE2 1

#if HAVE_SSE2
#include <emmintrin.h>
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <malloc.h>

#include "common.h"
#include "reedsolomon.h"
#include "diskfile.h"


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

    // Combine the four multiplication tables into two
    ALIGNED_16( uint16_t L[256] );
    ALIGNED_16( uint16_t H[256] );

    for ( int i = 0; i < 256; i++ )
    {
        L[i] = ll[fl][i] ^ hl[fh][i];
        H[i] = lh[fl][i] ^ hh[fh][i];
    }

#if HAVE_SSE2
#define LOAD_SL_SH(a,b,n) {\
        eax = _mm_extract_epi16( s, n ); \
        sl = eax & 0xff; \
        sh = (eax>>8) & 0xff; \
        a = _mm_insert_epi16( a, L[sl], n); \
        b = _mm_insert_epi16( b, H[sh], n); \
}

    for ( int i = 0; i < (length>>1); i+=8 ) // 8 shorts per iteration
    {
        __m128i s = _mm_load_si128( (__m128i const*)&slice[i] );
        __m128i d = _mm_load_si128( (__m128i const*)&dest[i] );

        uint32_t eax, sl, sh;
        __m128i xmm0, xmm1;

        LOAD_SL_SH(xmm0, xmm1, 0);
        LOAD_SL_SH(xmm0, xmm1, 1);
        LOAD_SL_SH(xmm0, xmm1, 2);
        LOAD_SL_SH(xmm0, xmm1, 3);
        LOAD_SL_SH(xmm0, xmm1, 4);
        LOAD_SL_SH(xmm0, xmm1, 5);
        LOAD_SL_SH(xmm0, xmm1, 6);
        LOAD_SL_SH(xmm0, xmm1, 7);

        xmm0 = _mm_xor_si128( xmm0, xmm1 );                // xmm0 = L[i..i+7] ^ H[i..i+7]
        d    = _mm_xor_si128( d, xmm0 );                   // d ^= xmm0

        _mm_store_si128( (__m128i *)&dest[i], d );
    }
#else
    for ( int i = 0; i < (length>>1); i++ )
    {
        uint16_t s = slice[i];

        uint16_t sl = (s >> 0) & 0xff;
        uint16_t sh = (s >> 8);

        dest[i] ^= L[sl] ^ H[sh];
    }
#endif
}

void rs_process( diskfile_t *files, int n_files, int block_start, int block_end, size_t blocksize, uint16_t **dest, progress_t *progress )
{
    // Set the destination buffers to zero
    int n_blocks = block_end - block_start + 1;
    for ( int d = 0; d < n_blocks; d++ )
        memset( dest[d], 0, blocksize );

    // Allocate read buffer
    uint16_t *slice = memalign( 16, (blocksize + 15) & ~15 );

    int col = 1;

    for ( int i = 0; i < n_files; i++ )
        for ( int j = 0; j < files[i].n_slices; j++ )
        {
            read_to_buf( &files[i], j*blocksize, blocksize, (void*)slice );
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

    free( slice );
}
