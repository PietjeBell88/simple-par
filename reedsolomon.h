#ifndef SPAR_REEDSOLOMON_H
#define SPAR_REEDSOLOMON_H

#include <stdint.h>
#include "diskfile.h"

void setup_tables();
void free_tables();

void recoveryslice( diskfile_t *files, int n_files, uint16_t blocknum, size_t length, uint16_t *dest );

#endif