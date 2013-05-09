#ifndef SPAR_DISKFILE_H
#define SPAR_DISKFILE_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "common.h"
#include "spar2.h"

struct spar_diskfile_t
{
    char *filename;
    char *virtual_filename;

    size_t offset;     // When splitting
    size_t filesize;   // File size
    uint16_t n_slices; // Number of slices

    md5_t hash_full;
    md5_t hash_16k;
    checksum_t *checksums;
};

void spar_add_input_file( spar_param_t *param, char *filename, char *virtual_filename, size_t offset, size_t filesize );

size_t read_to_buf( spar_diskfile_t *file, long int offset, size_t length, char *buf );

#endif