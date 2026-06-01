#include "numeral.h"

int numeral_value(uint32_t cp)
{
    switch (cp) {
    case 0x3007: /* 〇 */
    case 0x96F6: /* 零 */
        return 0;
    case 0x4E00: return 1; /* 一 */
    case 0x4E8C: return 2; /* 二 */
    case 0x4E09: return 3; /* 三 */
    case 0x56DB: return 4; /* 四 */
    case 0x4E94: return 5; /* 五 */
    case 0x516D: return 6; /* 六 */
    case 0x4E03: return 7; /* 七 */
    case 0x516B: return 8; /* 八 */
    case 0x4E5D: return 9; /* 九 */
    default:
        return -1;
    }
}

int numeral_is_digit(uint32_t cp)
{
    if (numeral_value(cp) >= 0) {
        return 1;
    }

    return cp == 0x5341 || cp == 0x767E || cp == 0x5343 ||
           cp == 0x842C || cp == 0x5104;
}

static int unit_value(uint32_t cp)
{
    switch (cp) {
    case 0x5341: return 10;   /* 十 */
    case 0x767E: return 100;  /* 百 */
    case 0x5343: return 1000; /* 千 */
    default: return 0;
    }
}

static int unit_rank(int unit)
{
    switch (unit) {
    case 1000: return 3;
    case 100: return 2;
    case 10: return 1;
    default: return 0;
    }
}

static int parse_section(const uint32_t *cps, size_t start, size_t end,
                         int allow_leading_zero, long *value)
{
    long total = 0;
    int pending = -1;
    int last_rank = 4;
    int zero_pending = 0;
    size_t i;

    if (start >= end) {
        return 0;
    }

    for (i = start; i < end; i++) {
        int v = numeral_value(cps[i]);
        int unit;
        int rank;

        if (v > 0) {
            if (pending >= 0) {
                return 0;
            }
            pending = v;
            zero_pending = 0;
            continue;
        }

        if (v == 0) {
            if (i == start && allow_leading_zero) {
                if (i + 1 >= end) {
                    return 0;
                }
                zero_pending = 1;
                continue;
            }
            if (zero_pending || last_rank <= 1 || last_rank == 4 || i + 1 >= end) {
                return 0;
            }
            zero_pending = 1;
            continue;
        }

        unit = unit_value(cps[i]);
        if (unit == 0) {
            return 0;
        }
        if (zero_pending) {
            return 0;
        }
        rank = unit_rank(unit);
        if (rank >= last_rank) {
            return 0;
        }

        total += (pending > 0 ? pending : 1) * unit;
        pending = -1;
        last_rank = rank;
    }

    if (zero_pending) {
        return 0;
    }

    if (pending > 0) {
        total += pending;
    }

    if (total <= 0 || total > 9999) {
        return 0;
    }

    *value = total;
    return 1;
}

int numeral_parse_long(const uint32_t *cps, size_t n, long *value)
{
    size_t i;
    size_t start = 0;
    long total = 0;
    int last_rank = 3;

    if (n == 0) {
        return 0;
    }

    if (n == 1 && numeral_value(cps[0]) == 0) {
        *value = 0;
        return 1;
    }

    for (i = 0; i < n; i++) {
        long section;
        long scale = 0;
        int rank = 0;

        if (cps[i] == 0x5104) {      /* 億 */
            scale = 100000000L;
            rank = 2;
        } else if (cps[i] == 0x842C) { /* 萬 */
            scale = 10000L;
            rank = 1;
        }

        if (scale > 0) {
            if (rank >= last_rank || i == start ||
                !parse_section(cps, start, i, 0, &section)) {
                return 0;
            }
            total += section * scale;
            start = i + 1;
            last_rank = rank;
        }
    }

    if (start == 0) {
        return parse_section(cps, 0, n, 0, value);
    }

    if (start < n) {
        long section;
        if (!parse_section(cps, start, n, 1, &section)) {
            return 0;
        }
        total += section;
    }

    *value = total;
    return 1;
}

long numeral_to_long(const uint32_t *cps, size_t n)
{
    long value = 0;

    (void)numeral_parse_long(cps, n, &value);
    return value;
}
