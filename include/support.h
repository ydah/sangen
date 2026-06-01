#ifndef SANGEN_SUPPORT_H
#define SANGEN_SUPPORT_H

#include <stdarg.h>
#include <stddef.h>

void *sangen_xmalloc(size_t size);
void *sangen_xrealloc(void *ptr, size_t size);
char *sangen_xstrdup(const char *s);
char *sangen_xstrndup(const char *s, size_t n);
char *sangen_vformat(const char *fmt, va_list ap);
char *sangen_format(const char *fmt, ...);

#endif
