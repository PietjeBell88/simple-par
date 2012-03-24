#ifndef SPAR_DISKFILE_H
#define SPAR_DISKFILE_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct
{
    char *filename;

    size_t offset;     // When splitting
    size_t filesize;   // File size
    uint16_t n_slices; // Number of slices
} diskfile_t;


void read_to_buf( diskfile_t *file, long int offset, size_t length, char *buf );

#endif