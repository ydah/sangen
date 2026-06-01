#include "utf8.h"

static int is_cont(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

size_t utf8_next(const unsigned char *buf, size_t len, size_t pos, uint32_t *cp)
{
    int valid;

    return utf8_next_checked(buf, len, pos, cp, &valid);
}

size_t utf8_next_checked(const unsigned char *buf, size_t len, size_t pos,
                         uint32_t *cp, int *valid)
{
    unsigned char c;

    *valid = 1;
    if (pos >= len) {
        *cp = 0;
        return 0;
    }

    c = buf[pos];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }

    if ((c & 0xE0) == 0xC0 && pos + 1 < len && is_cont(buf[pos + 1])) {
        *cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(buf[pos + 1] & 0x3F);
        if (*cp < 0x80) {
            *valid = 0;
        }
        return 2;
    }

    if ((c & 0xF0) == 0xE0 && pos + 2 < len &&
        is_cont(buf[pos + 1]) && is_cont(buf[pos + 2])) {
        *cp = ((uint32_t)(c & 0x0F) << 12) |
              ((uint32_t)(buf[pos + 1] & 0x3F) << 6) |
              (uint32_t)(buf[pos + 2] & 0x3F);
        if (*cp < 0x800 || (*cp >= 0xD800 && *cp <= 0xDFFF)) {
            *valid = 0;
        }
        return 3;
    }

    if ((c & 0xF8) == 0xF0 && pos + 3 < len &&
        is_cont(buf[pos + 1]) && is_cont(buf[pos + 2]) && is_cont(buf[pos + 3])) {
        *cp = ((uint32_t)(c & 0x07) << 18) |
              ((uint32_t)(buf[pos + 1] & 0x3F) << 12) |
              ((uint32_t)(buf[pos + 2] & 0x3F) << 6) |
              (uint32_t)(buf[pos + 3] & 0x3F);
        if (*cp < 0x10000 || *cp > 0x10FFFF) {
            *valid = 0;
        }
        return 4;
    }

    *cp = 0xFFFD;
    *valid = 0;
    return 1;
}

size_t utf8_encode(uint32_t cp, char out[5])
{
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        out[1] = '\0';
        return 1;
    }

    if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = '\0';
        return 2;
    }

    if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = '\0';
        return 3;
    }

    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    out[4] = '\0';
    return 4;
}
