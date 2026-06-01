#ifndef SANGEN_UTF8_H
#define SANGEN_UTF8_H

#include <stddef.h>
#include <stdint.h>

size_t utf8_next(const unsigned char *buf, size_t len, size_t pos, uint32_t *cp);
size_t utf8_next_checked(const unsigned char *buf, size_t len, size_t pos,
                         uint32_t *cp, int *valid);
size_t utf8_encode(uint32_t cp, char out[5]);

#endif
