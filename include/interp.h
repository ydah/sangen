#ifndef SANGEN_INTERP_H
#define SANGEN_INTERP_H

#include "ast.h"

typedef struct {
    long vars[10];
    int defined[10];
    Node **funcs;
    int nfunc;
    char *error;
    int failed;
} Env;

void env_init(Env *env);
void env_free(Env *env);
int interp_program(Node *program, Env *env);
long eval(Node *e, Env *env);
void exec(Node *s, Env *env);

#endif
