#ifndef SPAR_DISKFILE_H
#define SPAR_DISKFILE_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "common.h"

typedef struct
{
    char *filename;

    size_t offset;     // When splitting
    size_t filesize;   // File size
    uint16_t n_slices; // Number of slices

    md5_t hash_full;
    md5_t hash_16k;
    checksum_t *checksums;
} diskfile_t;


size_t read_to_buf( diskfile_t *file, long int offset, size_t length, char *buf );

#endif