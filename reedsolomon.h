#ifndef SPAR_REEDSOLOMON_H
#define SPAR_REEDSOLOMON_H

#include <stdint.h>
#include "diskfile.h"
#include "common.h"

void setup_tables();

void rs_process( spar_diskfile_t *files, int n_files, int block_start, int block_end, size_t blocksize, uint16_t **dest, progress_t *progress );

#endif