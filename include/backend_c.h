#ifndef SANGEN_BACKEND_C_H
#define SANGEN_BACKEND_C_H

#include <stdio.h>
#include "ast.h"

void emit_c_program(const Node *program, FILE *out);

#endif
