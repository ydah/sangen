#ifndef SANGEN_SURFACE_H
#define SANGEN_SURFACE_H

#include <stdint.h>
#include <stdio.h>

#include "lexer.h"

typedef struct {
    unsigned long start;
    unsigned long end;
    int line;
    int col;
} SourceSpan;

typedef enum {
    MARK_KAERI_RE
} MarkKind;

typedef struct {
    MarkKind kind;
    int boundary;
    int raw_index;
    SourceSpan span;
    uint32_t spelling;
} SourceMark;

typedef struct {
    TokenArray base_tokens;
    SourceMark *marks;
    int mark_count;
    int mark_capacity;
} SurfaceStream;

int surface_build(const TokenArray *raw, SurfaceStream *surface, char **error);
void surface_free(SurfaceStream *surface);
int surface_has_marks(const SurfaceStream *surface);
void surface_print_normalized(const TokenArray *raw, FILE *out);
void surface_print_marks(const SurfaceStream *surface, FILE *out);

#endif
