#include "lexer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"
#include "numeral.h"
#include "support.h"
#include "utf8.h"

typedef struct {
    const uint32_t *cps;
    int n;
    TokType type;
} Keyword;

static const uint32_t KW_FUNC_END[] = {0x8853, 0x7562};
static const uint32_t KW_TEXT_START[] = {0x8FAD, 0x66F0};
static const uint32_t KW_TEXT_END[] = {0x8FAD, 0x7562};
static const uint32_t KW_NOTE_START[] = {0x6CE8, 0x66F0};
static const uint32_t KW_NOTE_END[] = {0x6CE8, 0x7562};
static const uint32_t KW_IF_END[] = {0x5DF2, 0x77E3};
static const uint32_t KW_NOT_THEN[] = {0x4E0D, 0x7136};
static const uint32_t KW_NO_REM[] = {0x7121, 0x9918};
static const uint32_t KW_HAS_REM[] = {0x6709, 0x9918};
static const uint32_t KW_OVER[] = {0x904E};
static const uint32_t KW_NOT_REACH[] = {0x4E0D, 0x53CA};
static const uint32_t KW_FAN[] = {0x51E1};
static const uint32_t KW_FROM[] = {0x81EA};
static const uint32_t KW_TO[] = {0x81F3};
static const uint32_t KW_LOOP_END[] = {0x7109};
static const uint32_t KW_WHEN[] = {0x7576};
static const uint32_t KW_IF[] = {0x82E5};
static const uint32_t KW_THEN[] = {0x5247};
static const uint32_t KW_LET[] = {0x4EE4};
static const uint32_t KW_BE_TRAD[] = {0x7232};
static const uint32_t KW_SAY[] = {0x66F0};
static const uint32_t KW_WRITE[] = {0x66F8};
static const uint32_t KW_BY[] = {0x4EE5};
static const uint32_t KW_AND[] = {0x800C};
static const uint32_t KW_DIVIDE[] = {0x9664};
static const uint32_t KW_EQ[] = {0x7B49};
static const uint32_t KW_WITH[] = {0x8207};
static const uint32_t KW_GEN[] = {0x4E4B};
static const uint32_t KW_SUM[] = {0x548C};
static const uint32_t KW_DIFF[] = {0x5DEE};
static const uint32_t KW_PROD[] = {0x7A4D};
static const uint32_t KW_QUOT[] = {0x5546};
static const uint32_t KW_REM[] = {0x9918};
static const uint32_t KW_FUNC_START[] = {0x592B};
static const uint32_t KW_PROC[] = {0x8853};
static const uint32_t KW_SUBJECT[] = {0x8005};
static const uint32_t KW_ASSERT[] = {0x4E5F};
static const uint32_t KW_ACCEPT[] = {0x53D7};
static const uint32_t KW_RETURN[] = {0x7B54};
static const uint32_t KW_USE[] = {0x7528};

static const Keyword KEYWORDS[] = {
    {KW_FUNC_END, 2, T_FUNC_END},
    {KW_IF_END, 2, T_IF_END},
    {KW_NOT_THEN, 2, T_ELSE},
    {KW_NO_REM, 2, T_NO_REM},
    {KW_HAS_REM, 2, T_HAS_REM},
    {KW_NOT_REACH, 2, T_LT},
    {KW_FAN, 1, T_FAN},
    {KW_FROM, 1, T_FROM},
    {KW_TO, 1, T_TO},
    {KW_LOOP_END, 1, T_LOOP_END},
    {KW_WHEN, 1, T_WHILE},
    {KW_IF, 1, T_IF},
    {KW_THEN, 1, T_THEN},
    {KW_LET, 1, T_MAKE},
    {KW_BE_TRAD, 1, T_BE},
    {KW_SAY, 1, T_SAY},
    {KW_WRITE, 1, T_WRITE},
    {KW_BY, 1, T_BY},
    {KW_AND, 1, T_AND},
    {KW_DIVIDE, 1, T_DIVIDE},
    {KW_OVER, 1, T_GT},
    {KW_EQ, 1, T_EQ},
    {KW_WITH, 1, T_WITH},
    {KW_GEN, 1, T_GEN},
    {KW_SUM, 1, T_SUM},
    {KW_DIFF, 1, T_DIFF},
    {KW_PROD, 1, T_PROD},
    {KW_QUOT, 1, T_QUOT},
    {KW_REM, 1, T_REM},
    {KW_FUNC_START, 1, T_FUNC_START},
    {KW_PROC, 1, T_PROC},
    {KW_SUBJECT, 1, T_SUBJECT},
    {KW_ASSERT, 1, T_ASSERT},
    {KW_ACCEPT, 1, T_ACCEPT},
    {KW_RETURN, 1, T_RETURN},
    {KW_USE, 1, T_USE}
};

static int token_push(TokenArray *tokens, Token token)
{
    if (tokens->count == tokens->capacity) {
        int next = tokens->capacity ? tokens->capacity * 2 : 64;
        tokens->data = sangen_xrealloc(tokens->data, sizeof(Token) * (size_t)next);
        tokens->capacity = next;
    }

    tokens->data[tokens->count++] = token;
    return 1;
}

static int set_utf8_error(int line, int col, char **error)
{
    char label[128];

    sangen_pos_label(line, col, label, sizeof(label));
    *error = sangen_format("%s萬國碼不成", label);
    return 0;
}

static int is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == 0x3000;
}

static int var_index(uint32_t cp)
{
    static const uint32_t vars[] = {
        0x7532, 0x4E59, 0x4E19, 0x4E01, 0x620A,
        0x5DF1, 0x5E9A, 0x8F9B, 0x58EC, 0x7678
    };
    int i;

    for (i = 0; i < 10; i++) {
        if (cp == vars[i]) {
            return i;
        }
    }

    return -1;
}

static const char *var_label(int var)
{
    static const char *names[] = {
        "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"
    };

    if (var < 0 || var >= 10) {
        return "?";
    }

    return names[var];
}

static int match_cps(const unsigned char *buf, size_t len, size_t pos,
                     const uint32_t *cps, int n, size_t *consumed)
{
    size_t p = pos;
    int i;

    for (i = 0; i < n; i++) {
        uint32_t cp;
        size_t step = utf8_next(buf, len, p, &cp);

        if (step == 0 || cp != cps[i]) {
            return 0;
        }

        p += step;
    }

    *consumed = p - pos;
    return 1;
}

static int match_keyword(const unsigned char *buf, size_t len, size_t pos,
                         TokType *type, size_t *consumed, int *chars)
{
    size_t i;

    for (i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
        if (match_cps(buf, len, pos, KEYWORDS[i].cps, KEYWORDS[i].n, consumed)) {
            *type = KEYWORDS[i].type;
            if (chars) {
                *chars = KEYWORDS[i].n;
            }
            return 1;
        }
    }

    return 0;
}

static int is_identifier_char(uint32_t cp)
{
    return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF);
}

static int starts_ident_boundary(const unsigned char *buf, size_t len, size_t pos)
{
    uint32_t cp;
    size_t step = utf8_next(buf, len, pos, &cp);
    TokType type;
    size_t consumed;

    if (step == 0) {
        return 1;
    }

    if (!match_keyword(buf, len, pos, &type, &consumed, NULL)) {
        return 0;
    }

    return type == T_PROC || type == T_SUBJECT ||
           type == T_IF_END || type == T_ELSE ||
           type == T_LOOP_END || type == T_RETURN || type == T_FUNC_END;
}

static char *cp_to_string(uint32_t cp)
{
    char tmp[5];
    utf8_encode(cp, tmp);
    return sangen_xstrndup(tmp, strlen(tmp));
}

static void bytes_append(char **buf, size_t *len, size_t *cap,
                         const unsigned char *src, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t next = *cap ? *cap : 16;

        while (*len + n + 1 > next) {
            next *= 2;
        }
        *buf = sangen_xrealloc(*buf, next);
        *cap = next;
    }

    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
}

static int match_text_end(const unsigned char *buf, size_t len, size_t pos,
                          size_t *consumed)
{
    return match_cps(buf, len, pos, KW_TEXT_END, 2, consumed);
}

static int match_note_end(const unsigned char *buf, size_t len, size_t pos,
                          size_t *consumed)
{
    return match_cps(buf, len, pos, KW_NOTE_END, 2, consumed);
}

static int read_string(const unsigned char *buf, size_t len, size_t *pos,
                       int *line, int *col, TokenArray *out, char **error)
{
    size_t p;
    size_t mark_len;
    char *text = NULL;
    size_t text_len = 0;
    size_t text_cap = 0;
    int start_line = *line;
    int start_col = *col;
    size_t start_pos = *pos;
    Token tok;

    match_cps(buf, len, *pos, KW_TEXT_START, 2, &mark_len);
    *pos += mark_len;
    *col += 2;
    p = *pos;

    while (p < len) {
        uint32_t cp;
        int valid;
        size_t step = utf8_next_checked(buf, len, p, &cp, &valid);
        size_t end_len;

        if (step == 0) {
            break;
        }
        if (!valid) {
            free(text);
            return set_utf8_error(*line, *col, error);
        }

        if (match_text_end(buf, len, p, &end_len)) {
            size_t next_len;

            if (match_text_end(buf, len, p + end_len, &next_len)) {
                bytes_append(&text, &text_len, &text_cap, buf + p, end_len);
                p += end_len + next_len;
                *col += 4;
                continue;
            }

            tok.type = T_STRING;
            tok.lval = 0;
            tok.sval = text ? text : sangen_xstrndup("", 0);
            tok.line = start_line;
            tok.col = start_col;
            tok.start = (unsigned long)start_pos;
            tok.end = (unsigned long)(p + end_len);
            token_push(out, tok);
            *pos = p + end_len;
            *col += 2;
            return 1;
        }

        bytes_append(&text, &text_len, &text_cap, buf + p, step);
        if (cp == '\n') {
            (*line)++;
            *col = 1;
        } else {
            (*col)++;
        }

        p += step;
    }

    {
        char label[128];
        sangen_pos_label(start_line, start_col, label, sizeof(label));
        *error = sangen_format("%s辭不閉", label);
    }
    free(text);
    return 0;
}

static int skip_comment(const unsigned char *buf, size_t len, size_t *pos,
                        int *line, int *col, char **error)
{
    size_t mark_len;

    match_cps(buf, len, *pos, KW_NOTE_START, 2, &mark_len);
    *pos += mark_len;
    *col += 2;

    while (*pos < len) {
        uint32_t cp;
        int valid;
        size_t step = utf8_next_checked(buf, len, *pos, &cp, &valid);
        size_t end_len;

        if (step == 0) {
            break;
        }
        if (!valid) {
            return set_utf8_error(*line, *col, error);
        }

        if (match_note_end(buf, len, *pos, &end_len)) {
            *pos += end_len;
            *col += 2;
            return 1;
        }

        *pos += step;
        if (cp == '\n') {
            (*line)++;
            *col = 1;
        } else {
            (*col)++;
        }
    }

    {
        char label[128];
        sangen_pos_label(*line, *col, label, sizeof(label));
        *error = sangen_format("%s注不畢", label);
    }
    return 0;
}

static int read_number(const unsigned char *buf, size_t len, size_t *pos,
                       int line, int *col, TokenArray *out, char **error)
{
    uint32_t *cps = NULL;
    size_t count = 0;
    size_t cap = 0;
    long value;
    int start_col = *col;
    size_t start_pos = *pos;
    Token tok;

    while (*pos < len) {
        uint32_t cp;
        size_t step = utf8_next(buf, len, *pos, &cp);

        if (step == 0 || !numeral_is_digit(cp)) {
            break;
        }

        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            cps = sangen_xrealloc(cps, sizeof(uint32_t) * cap);
        }

        cps[count++] = cp;
        *pos += step;
        (*col)++;
    }

    if (!numeral_parse_long(cps, count, &value)) {
        char label[128];
        sangen_pos_label(line, start_col, label, sizeof(label));
        *error = sangen_format("%s數辭不成", label);
        free(cps);
        return 0;
    }

    if (value > 999999999999L) {
        char label[128];
        sangen_pos_label(line, start_col, label, sizeof(label));
        *error = sangen_format("%s數過其限", label);
        free(cps);
        return 0;
    }

    tok.type = T_NUMBER;
    tok.lval = value;
    tok.sval = NULL;
    tok.line = line;
    tok.col = start_col;
    tok.start = (unsigned long)start_pos;
    tok.end = (unsigned long)*pos;
    free(cps);
    token_push(out, tok);
    return 1;
}

static int read_ident(const unsigned char *buf, size_t len, size_t *pos, int line,
                      int *col, TokenArray *out)
{
    size_t start = *pos;
    size_t p = *pos;
    int start_col = *col;
    int count = 0;
    Token tok;

    while (p < len) {
        uint32_t cp;
        size_t step = utf8_next(buf, len, p, &cp);

        if (step == 0 || !is_identifier_char(cp)) {
            break;
        }

        if (p != start && starts_ident_boundary(buf, len, p)) {
            break;
        }

        p += step;
        count++;
    }

    tok.type = T_IDENT;
    tok.lval = 0;
    tok.sval = sangen_xstrndup((const char *)buf + start, p - start);
    tok.line = line;
    tok.col = start_col;
    tok.start = (unsigned long)start;
    tok.end = (unsigned long)p;
    token_push(out, tok);
    *pos = p;
    *col += count;
    return 1;
}

int lex_source(const unsigned char *buf, size_t len, TokenArray *out, char **error)
{
    size_t pos = 0;
    int line = 1;
    int col = 1;

    out->data = NULL;
    out->count = 0;
    out->capacity = 0;
    *error = NULL;

    while (pos < len) {
        uint32_t cp;
        int valid;
        size_t step = utf8_next_checked(buf, len, pos, &cp, &valid);
        int var;
        TokType type;
        size_t consumed;
        int token_chars;
        Token tok;

        if (step == 0) {
            break;
        }
        if (!valid) {
            set_utf8_error(line, col, error);
            return 0;
        }

        if (is_space(cp)) {
            if (cp == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
            pos += step;
            continue;
        }

        if (match_cps(buf, len, pos, KW_NOTE_START, 2, &consumed)) {
            if (!skip_comment(buf, len, &pos, &line, &col, error)) {
                return 0;
            }
            continue;
        }

        if (match_cps(buf, len, pos, KW_TEXT_START, 2, &consumed)) {
            if (!read_string(buf, len, &pos, &line, &col, out, error)) {
                return 0;
            }
            continue;
        }

        if (numeral_is_digit(cp)) {
            if (!read_number(buf, len, &pos, line, &col, out, error)) {
                return 0;
            }
            continue;
        }

        var = var_index(cp);
        if (var >= 0) {
            tok.type = T_VAR;
            tok.lval = var;
            tok.sval = NULL;
            tok.line = line;
            tok.col = col;
            tok.start = (unsigned long)pos;
            token_push(out, tok);
            pos += step;
            col++;
            out->data[out->count - 1].end = (unsigned long)pos;
            continue;
        }

        if (match_keyword(buf, len, pos, &type, &consumed, &token_chars)) {
            tok.type = type;
            tok.lval = 0;
            tok.sval = NULL;
            tok.line = line;
            tok.col = col;
            tok.start = (unsigned long)pos;
            tok.end = (unsigned long)(pos + consumed);
            token_push(out, tok);
            pos += consumed;
            col += token_chars;
            continue;
        }

        if (is_identifier_char(cp)) {
            read_ident(buf, len, &pos, line, &col, out);
            continue;
        }

        {
            char *bad = cp_to_string(cp);
            char label[128];
            sangen_pos_label(line, col, label, sizeof(label));
            *error = sangen_format("%s有難識之字%s", label, bad);
            free(bad);
            return 0;
        }
    }

    {
        Token eof;
        eof.type = T_EOF;
        eof.lval = 0;
        eof.sval = NULL;
        eof.line = line;
        eof.col = col;
        eof.start = (unsigned long)pos;
        eof.end = (unsigned long)pos;
        token_push(out, eof);
    }

    return 1;
}

void tokens_free(TokenArray *tokens)
{
    int i;

    if (!tokens) {
        return;
    }

    for (i = 0; i < tokens->count; i++) {
        free(tokens->data[i].sval);
    }

    free(tokens->data);
    tokens->data = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}

void tokens_print(const TokenArray *tokens)
{
    int i;

    for (i = 0; i < tokens->count; i++) {
        const Token *tok = &tokens->data[i];
        char line[128];
        sangen_line_label(tok->line, line, sizeof(line));
        printf("%s  %s", line, tok_type_name(tok->type));
        if (tok->type == T_NUMBER) {
            char num[128];
            sangen_number_label(tok->lval, num, sizeof(num));
            printf(" %s", num);
        } else if (tok->type == T_VAR) {
            printf(" %s", var_label((int)tok->lval));
        } else if (tok->type == T_STRING || tok->type == T_IDENT) {
            printf(" %s", tok->sval);
        }
        printf("\n");
    }
}
