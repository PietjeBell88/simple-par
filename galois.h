#ifndef SPAR_GALOIS_H
#define SPAR_GALOIS_H

#define NW (1 << 16)
#define PRIM_POLY 0x1100B;

#include <stdint.h>
#include <stdlib.h>

uint16_t *gflog, *gfilog, *vander;

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

void setup_tables()
{
    gflog  = (uint16_t*) malloc( sizeof(uint16_t) * NW );
    gfilog = (uint16_t*) malloc( sizeof(uint16_t) * NW );
    vander = (uint16_t*) malloc( sizeof(uint16_t) * NW/2 );

    int log = 0;
    unsigned b = 1, v = 0;
    int n = 0;

    for ( ; log < NW-1; log++ ) {
        // Write the log table
        gflog[b] = (uint16_t)log;
        gfilog[log] = (uint16_t)b;
        b = b << 1;
        if (b & NW) b = b ^ PRIM_POLY;

        // Add possible vanderpol constants
        v = log + 1;
        if (v%3 != 0 && v%5 != 0 && v%17 != 0 && v%257 != 0) {
            vander[n] = v;
            n += 1;
        }
    }
}

void free_tables()
{
    free( gflog );
    free( gfilog );
    free( vander );
}

#endif



