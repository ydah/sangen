#ifndef SANGEN_PARSER_H
#define SANGEN_PARSER_H

#include "ast.h"
#include "token.h"

Node *parse_program(const Token *tokens, int count, char **error);

#endif
