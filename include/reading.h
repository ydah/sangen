#ifndef SANGEN_READING_H
#define SANGEN_READING_H

#include <stdio.h>

#include "surface.h"

typedef struct {
    int *indices;
    int count;
} ReadingOrder;

int derive_reading_order(const SurfaceStream *surface, ReadingOrder *out,
                         char **error);
void reading_order_free(ReadingOrder *order);
void reading_order_print(const SurfaceStream *surface,
                         const ReadingOrder *order, FILE *out);

#endif
