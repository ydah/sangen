#include "support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *sangen_xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);

    if (!p) {
        fprintf(stderr, "所藏不足\n");
        exit(EXIT_FAILURE);
    }

    return p;
}

void *sangen_xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);

    if (!p) {
        fprintf(stderr, "所藏不足\n");
        exit(EXIT_FAILURE);
    }

    return p;
}

char *sangen_xstrdup(const char *s)
{
    return sangen_xstrndup(s, strlen(s));
}

char *sangen_xstrndup(const char *s, size_t n)
{
    char *copy = sangen_xmalloc(n + 1);

    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

char *sangen_vformat(const char *fmt, va_list ap)
{
    char stack[256];
    char *heap;
    va_list ap2;
    int n;

    va_copy(ap2, ap);
    n = vsnprintf(stack, sizeof(stack), fmt, ap);
    if (n < 0) {
        va_end(ap2);
        return sangen_xstrdup("中有誤");
    }

    if ((size_t)n < sizeof(stack)) {
        va_end(ap2);
        return sangen_xstrndup(stack, (size_t)n);
    }

    heap = sangen_xmalloc((size_t)n + 1);
    vsnprintf(heap, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return heap;
}

char *sangen_format(const char *fmt, ...)
{
    char *s;
    va_list ap;

    va_start(ap, fmt);
    s = sangen_vformat(fmt, ap);
    va_end(ap);
    return s;
}
