#ifndef SANGEN_MESSAGE_H
#define SANGEN_MESSAGE_H

#include <stddef.h>

void sangen_line_label(int line, char *buf, size_t size);
void sangen_pos_label(int line, int col, char *buf, size_t size);
void sangen_number_label(long value, char *buf, size_t size);

#endif
