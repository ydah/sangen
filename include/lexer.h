#ifndef SANGEN_LEXER_H
#define SANGEN_LEXER_H

#include <stddef.h>
#include "token.h"

typedef struct {
    Token *data;
    int count;
    int capacity;
} TokenArray;

int lex_source(const unsigned char *buf, size_t len, TokenArray *out, char **error);
void tokens_free(TokenArray *tokens);
void tokens_print(const TokenArray *tokens);

#endif
