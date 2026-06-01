#include "check.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"
#include "support.h"

typedef struct {
    const Node *program;
    char *error;
} Checker;

static int fail(Checker *c, int line, const char *fmt, ...)
{
    char body[256];
    char label[128];
    va_list ap;
    int n;

    if (c->error) {
        return 0;
    }

    va_start(ap, fmt);
    n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0) {
        strcpy(body, "義理不通");
    }

    sangen_line_label(line, label, sizeof(label));
    c->error = sangen_format("%s%s", label, body);
    return 0;
}

static const Node *find_func(const Checker *c, const char *name)
{
    int i;

    if (!c->program || c->program->kind != N_BLOCK) {
        return NULL;
    }

    for (i = 0; i < c->program->nstmt; i++) {
        const Node *stmt = c->program->stmts[i];

        if (stmt->kind == N_FUNC && stmt->name && strcmp(stmt->name, name) == 0) {
            return stmt;
        }
    }

    return NULL;
}

static int check_expr(Checker *c, const Node *node);
static int check_stmt(Checker *c, const Node *node, int allow_func);

static int check_top_funcs(Checker *c)
{
    int i;
    int j;

    if (!c->program || c->program->kind != N_BLOCK) {
        return 1;
    }

    for (i = 0; i < c->program->nstmt; i++) {
        const Node *func = c->program->stmts[i];

        if (func->kind != N_FUNC) {
            continue;
        }

        for (j = 0; j < i; j++) {
            const Node *prev = c->program->stmts[j];

            if (prev->kind == N_FUNC && prev->name && func->name &&
                strcmp(prev->name, func->name) == 0) {
                return fail(c, func->line, "術%s既立", func->name);
            }
        }
    }

    return 1;
}

static int check_params(Checker *c, const Node *func)
{
    int seen[10] = {0};
    int i;

    for (i = 0; i < func->nparam; i++) {
        int var = func->params[i];

        if (var < 0 || var >= 10) {
            return fail(c, func->line, "天干不可識");
        }
        if (seen[var]) {
            return fail(c, func->param_lines ? func->param_lines[i] : func->line,
                        "%s再受", var_name(var));
        }
        seen[var] = 1;
    }

    return 1;
}

static int check_expr(Checker *c, const Node *node)
{
    if (!node) {
        return 1;
    }

    switch (node->kind) {
    case N_NUM:
    case N_VAR:
        return 1;
    case N_BINEXPR:
        return check_expr(c, node->lhs) && check_expr(c, node->rhs);
    case N_DIVIS:
        return check_expr(c, node->divisor);
    case N_COMPARE:
        return check_expr(c, node->lhs) && check_expr(c, node->rhs);
    case N_CALL:
        if (!find_func(c, node->name ? node->name : "")) {
            return fail(c, node->line, "術%s未立", node->name ? node->name : "");
        }
        return 1;
    default:
        return 1;
    }
}

static int check_block(Checker *c, const Node *node, int allow_func)
{
    int i;

    if (!node) {
        return 1;
    }

    for (i = 0; i < node->nstmt; i++) {
        if (!check_stmt(c, node->stmts[i], allow_func)) {
            return 0;
        }
    }

    return 1;
}

static int check_stmt(Checker *c, const Node *node, int allow_func)
{
    int i;

    if (!node) {
        return 1;
    }

    switch (node->kind) {
    case N_BLOCK:
        return check_block(c, node, 0);
    case N_ASSIGN:
        return check_expr(c, node->expr);
    case N_SAY:
        return 1;
    case N_WRITE:
        return check_expr(c, node->expr);
    case N_FOR:
        return check_expr(c, node->from) && check_expr(c, node->to) &&
               check_block(c, node->body, 0);
    case N_WHILE:
        return check_expr(c, node->expr) && check_block(c, node->body, 0);
    case N_IF:
        for (i = 0; i < node->nbranch; i++) {
            if (!check_expr(c, node->conds[i]) || !check_block(c, node->thens[i], 0)) {
                return 0;
            }
        }
        return check_block(c, node->els, 0);
    case N_FUNC:
        if (!allow_func) {
            return fail(c, node->line, "術不可在句中立");
        }
        return check_params(c, node) && check_block(c, node->body, 0) &&
               check_expr(c, node->expr);
    case N_CALL:
        return check_expr(c, node);
    default:
        return 1;
    }
}

int check_program(const Node *program, char **error)
{
    Checker c;
    int i;

    c.program = program;
    c.error = NULL;

    if (!check_top_funcs(&c)) {
        *error = c.error;
        return 0;
    }

    if (program && program->kind == N_BLOCK) {
        for (i = 0; i < program->nstmt; i++) {
            if (!check_stmt(&c, program->stmts[i], 1)) {
                *error = c.error;
                return 0;
            }
        }
    } else if (!check_stmt(&c, program, 1)) {
        *error = c.error;
        return 0;
    }

    *error = NULL;
    return 1;
}
