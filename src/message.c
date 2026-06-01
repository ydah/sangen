#include "message.h"

#include <stdio.h>
#include <string.h>

static void append(char *buf, size_t size, const char *s)
{
    size_t used;

    if (size == 0) {
        return;
    }

    used = strlen(buf);
    if (used + 1 >= size) {
        return;
    }

    strncat(buf, s, size - used - 1);
}

static void append_digit(char *buf, size_t size, int digit)
{
    static const char *digits[] = {
        "零", "一", "二", "三", "四", "五", "六", "七", "八", "九"
    };

    append(buf, size, digits[digit]);
}

static void append_section(char *buf, size_t size, int section)
{
    static const int values[] = {1000, 100, 10, 1};
    static const char *units[] = {"千", "百", "十", ""};
    int i;
    int wrote = 0;
    int need_zero = 0;

    for (i = 0; i < 4; i++) {
        int value = values[i];
        int digit = section / value;
        section %= value;

        if (digit == 0) {
            if (wrote && section > 0) {
                need_zero = 1;
            }
            continue;
        }

        if (need_zero) {
            append(buf, size, "零");
            need_zero = 0;
        }

        if (!(value == 10 && digit == 1 && !wrote)) {
            append_digit(buf, size, digit);
        }
        append(buf, size, units[i]);
        wrote = 1;
    }
}

static void han_number(long value, char *buf, size_t size)
{
    long high;
    int mid;
    int low;

    if (size == 0) {
        return;
    }

    buf[0] = '\0';
    if (value <= 0) {
        append(buf, size, "零");
        return;
    }

    high = value / 100000000L;
    mid = (int)((value / 10000) % 10000);
    low = (int)(value % 10000);

    if (high > 0) {
        append_section(buf, size, (int)high);
        append(buf, size, "億");
        if (mid > 0 && mid < 1000) {
            append(buf, size, "零");
        }
    }

    if (mid > 0) {
        append_section(buf, size, mid);
        append(buf, size, "萬");
        if (low > 0 && low < 1000) {
            append(buf, size, "零");
        }
    } else if (high > 0 && low > 0) {
        append(buf, size, "零");
    }

    if (low > 0) {
        append_section(buf, size, low);
    }
}

void sangen_line_label(int line, char *buf, size_t size)
{
    char num[128];

    han_number(line, num, sizeof(num));
    snprintf(buf, size, "第%s行", num);
}

void sangen_pos_label(int line, int col, char *buf, size_t size)
{
    char line_num[128];
    char col_num[128];

    han_number(line, line_num, sizeof(line_num));
    han_number(col, col_num, sizeof(col_num));
    snprintf(buf, size, "第%s行第%s字", line_num, col_num);
}

void sangen_number_label(long value, char *buf, size_t size)
{
    han_number(value, buf, size);
}
