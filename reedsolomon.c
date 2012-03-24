#define NW (1 << 16)
#define PRIM_POLY 0x1100B;

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "reedsolomon.h"
#include "diskfile.h"

// Log/antilog tables
uint16_t *gflog, *gfilog, *vander;

// Lookup tables
uint16_t ll[256][256];
uint16_t lh[256][256];
uint16_t hl[256][256];
uint16_t hh[256][256];


uint16_t gfmult( uint16_t a, uint16_t b)
{
    uint32_t sum_log;
    if (a == 0 || b == 0) return 0;
    sum_log = gflog[a] + gflog[b];
    if (sum_log >= NW-1) sum_log -= NW-1;
    return gfilog[sum_log];
}

uint16_t gfdiv(int a, int b)
{
    int diff_log;
    if (a == 0) return 0;
    if (b == 0) return -1; /* Can’t divide by 0 */
    diff_log = gflog[a] - gflog[b];
    if (diff_log < 0) diff_log += NW-1;
    return gfilog[diff_log];
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
    gflog  = malloc( sizeof(uint16_t) * NW );
    gfilog = malloc( sizeof(uint16_t) * NW );
    vander = malloc( sizeof(uint16_t) * (NW/2 + 1) ); // Because we apparenty start at exponent 0 instead of 1

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

void free_tables()
{
    free( gflog );
    free( gfilog );
    free( vander );
}

void lookup_multiply( uint16_t f, uint16_t *slice, uint16_t *dest, int length )
{
    uint16_t fl = (f >> 0) & 0xff;
    uint16_t fh = (f >> 8);

    // Combine the four multiplication tables into two
    uint16_t L[256];
    uint16_t H[256];

    for ( int i = 0; i < 256; i++ )
    {
        L[i] = ll[fl][i] ^ hl[fh][i];
        H[i] = lh[fl][i] ^ hh[fh][i];
    }

    for ( int i = 0; i < (length>>1); i++ )
    {
        uint16_t s = slice[i];

        uint16_t sl = (s >> 0) & 0xff;
        uint16_t sh = (s >> 8);

        dest[i] ^= L[sl] ^ H[sh];
    }
}

void recoveryslice( diskfile_t *files, int n_files, uint16_t blocknum, size_t length, uint16_t *dest )
{
    // TODO: Probably can somehow do only the remainder
    memset( dest, 0, length );

    // Allocate buffer
    uint16_t *slice = malloc( length );

    int col = 1;

    for ( int i = 0; i < n_files; i++ )
        for ( int j = 0; j < files[i].n_slices; j++ )
        {
            read_to_buf( &files[i], j*length, length, (void*)slice );
            uint16_t current = gfpow(vander[col], blocknum);

            lookup_multiply( current, slice, dest, length );

            col += 1;
        }

    free( slice );
}


