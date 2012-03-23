#define NW (1 << 16)
#define PRIM_POLY 0x1100B;

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "reedsolomon.h"
#include "diskfile.h"

uint16_t *gflog, *gfilog, *vander;
uint32_t *exponents;

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
    gflog  = (uint16_t*) malloc( sizeof(uint16_t) * NW );
    gfilog = (uint16_t*) malloc( sizeof(uint16_t) * NW );
    vander = (uint16_t*) malloc( sizeof(uint16_t) * (NW/2 + 1) ); // Because we apparenty start at exponent 0 instead of 1
    exponents = (uint32_t*) malloc( sizeof(uint32_t) * (NW/2 + 1) );

    int log = 0;
    unsigned b = 1, v = 0;
    int n = 1;

    exponents[0] = 0;
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
            exponents[n] = v;
            vander[n] = b;
            n += 1;
        }

    }
}

void free_tables()
{
    free( gflog );
    free( gfilog );
    free( vander );
    free( exponents );
}




uint32_t recoveryslice( diskfile_t *files, int n_files, uint16_t blocknum, size_t length, uint16_t *dest )
{
    // TODO: Probably can somehow do only the remainder
    memset( dest, 0, length );

    // Allocate buffer
    uint16_t *slice = malloc( length );

    uint16_t constant = vander[blocknum];
    uint32_t exponent = exponents[blocknum];

    int col = 1;

    int slicenum = 0;

    for ( int i = 0; i < n_files; i++ )
        for ( int j = 0; j < files[i].n_slices; j++ )
        {
            read_to_buf( &files[i], j*length, length, (void*)slice );
            uint16_t current = gfpow(vander[col], blocknum);
            //printf( "ROW, COL, EXP, ELEM: %6d, %6d, %6d, %6d\n", blocknum, col-1, blocknum, current );
            for ( int k = 0; k < length/2; k++ )
                dest[k] ^= gfmult( current, slice[k] );

            col += 1;
        }

    free( slice );

    return exponents[blocknum];
}


