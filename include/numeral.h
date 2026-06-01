#ifndef SANGEN_NUMERAL_H
#define SANGEN_NUMERAL_H

#include <stddef.h>
#include <stdint.h>

int numeral_value(uint32_t cp);
int numeral_is_digit(uint32_t cp);
long numeral_to_long(const uint32_t *cps, size_t n);
int numeral_parse_long(const uint32_t *cps, size_t n, long *value);

#endif
