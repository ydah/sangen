#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"
#include "support.h"

typedef struct {
    const Token *tokens;
    int count;
    int pos;
    char *error;
} Parser;

static const Token *peek(Parser *p)
{
    if (p->pos >= p->count) {
        return &p->tokens[p->count - 1];
    }

    return &p->tokens[p->pos];
}

static const Token *previous(Parser *p)
{
    if (p->pos <= 0) {
        return &p->tokens[0];
    }

    return &p->tokens[p->pos - 1];
}

static int is_at(Parser *p, TokType type)
{
    return peek(p)->type == type;
}

static int match(Parser *p, TokType type)
{
    if (!is_at(p, type)) {
        return 0;
    }

    p->pos++;
    return 1;
}

static int fail(Parser *p, int line, const char *fmt, ...)
{
    char stack[256];
    va_list ap;
    int n;

    if (p->error) {
        return 0;
    }

    va_start(ap, fmt);
    n = vsnprintf(stack, sizeof(stack), fmt, ap);
    va_end(ap);

    if (n < 0) {
        char label[128];
        sangen_pos_label(line, peek(p)->col, label, sizeof(label));
        p->error = sangen_format("%s文法難識", label);
    } else {
        char label[128];
        sangen_pos_label(line, peek(p)->col, label, sizeof(label));
        p->error = sangen_format("%s%s", label, stack);
    }

    return 0;
}

static int expect(Parser *p, TokType type)
{
    if (match(p, type)) {
        return 1;
    }

    return fail(p, peek(p)->line, "闕%s", tok_type_name(type));
}

static int expect_var(Parser *p, int *var)
{
    if (!is_at(p, T_VAR)) {
        return fail(p, peek(p)->line, "闕天干");
    }

    *var = (int)peek(p)->lval;
    p->pos++;
    return 1;
}

static int expect_ident(Parser *p, char **name)
{
    if (!is_at(p, T_IDENT)) {
        return fail(p, peek(p)->line, "闕術之名");
    }

    *name = sangen_xstrdup(peek(p)->sval ? peek(p)->sval : "");
    p->pos++;
    return 1;
}

static int is_expr_op(TokType type)
{
    return type == T_SUM || type == T_DIFF || type == T_PROD ||
           type == T_QUOT || type == T_REM;
}

static int is_compare_op(TokType type)
{
    return type == T_GT || type == T_LT || type == T_EQ;
}

static Node *parse_item(Parser *p);
static Node *parse_expr(Parser *p);
static Node *parse_cond(Parser *p);

static Node *parse_block_until(Parser *p, TokType stop1, TokType stop2)
{
    Node *block = node_new(N_BLOCK, peek(p)->line);

    while (!is_at(p, T_EOF) && !is_at(p, stop1) && !is_at(p, stop2)) {
        Node *item = parse_item(p);
        if (!item) {
            ast_free(block);
            return NULL;
        }
        node_block_append(block, item);
    }

    return block;
}

static Node *parse_atom(Parser *p)
{
    const Token *tok = peek(p);
    Node *node;

    if (match(p, T_NUMBER)) {
        node = node_new(N_NUM, tok->line);
        node->num = tok->lval;
        return node;
    }

    if (match(p, T_VAR)) {
        node = node_new(N_VAR, tok->line);
        node->var = (int)tok->lval;
        return node;
    }

    if (match(p, T_USE)) {
        node = node_new(N_CALL, tok->line);
        if (!expect_ident(p, &node->name) || !expect(p, T_PROC)) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    fail(p, tok->line, "此處當書數天干或術");
    return NULL;
}

static Node *parse_expr(Parser *p)
{
    Node *lhs = parse_atom(p);
    Node *rhs;
    Node *node;
    const Token *op;

    if (!lhs) {
        return NULL;
    }

    if (!match(p, T_WITH)) {
        return lhs;
    }

    rhs = parse_atom(p);
    if (!rhs) {
        ast_free(lhs);
        return NULL;
    }

    if (!expect(p, T_GEN)) {
        ast_free(lhs);
        ast_free(rhs);
        return NULL;
    }

    op = peek(p);
    if (!is_expr_op(op->type)) {
        fail(p, op->line, "闕和差積商餘之一");
        ast_free(lhs);
        ast_free(rhs);
        return NULL;
    }
    p->pos++;

    node = node_new(N_BINEXPR, lhs->line);
    node->lhs = lhs;
    node->rhs = rhs;
    node->op = op->type;
    return node;
}

static Node *parse_divis(Parser *p)
{
    Node *node;
    Node *divisor;
    int dividend;
    int want_no_rem;
    int line = previous(p)->line;

    divisor = parse_expr(p);
    if (!divisor) {
        return NULL;
    }

    if (!expect(p, T_DIVIDE) || !expect_var(p, &dividend)) {
        ast_free(divisor);
        return NULL;
    }

    if (!expect(p, T_AND)) {
        ast_free(divisor);
        return NULL;
    }

    if (match(p, T_NO_REM)) {
        want_no_rem = 1;
    } else if (match(p, T_HAS_REM)) {
        want_no_rem = 0;
    } else {
        fail(p, peek(p)->line, "闕無餘若有餘");
        ast_free(divisor);
        return NULL;
    }

    node = node_new(N_DIVIS, line);
    node->divisor = divisor;
    node->dividend = dividend;
    node->want_no_rem = want_no_rem;
    return node;
}

static Node *parse_compare(Parser *p)
{
    Node *lhs = parse_expr(p);
    Node *rhs;
    Node *node;
    const Token *op;

    if (!lhs) {
        return NULL;
    }

    op = peek(p);
    if (!is_compare_op(op->type)) {
        fail(p, op->line, "闕比較之辭");
        ast_free(lhs);
        return NULL;
    }
    p->pos++;

    rhs = parse_expr(p);
    if (!rhs) {
        ast_free(lhs);
        return NULL;
    }

    node = node_new(N_COMPARE, lhs->line);
    node->lhs = lhs;
    node->rhs = rhs;
    node->op = op->type;
    return node;
}

static Node *parse_cond(Parser *p)
{
    if (match(p, T_BY)) {
        return parse_divis(p);
    }

    return parse_compare(p);
}

static Node *parse_simple(Parser *p)
{
    const Token *tok = peek(p);

    if (match(p, T_MAKE)) {
        Node *node = node_new(N_ASSIGN, tok->line);
        if (!expect_var(p, &node->var) || !expect(p, T_BE)) {
            ast_free(node);
            return NULL;
        }
        node->expr = parse_expr(p);
        if (!node->expr) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (match(p, T_SAY)) {
        Node *node = node_new(N_SAY, tok->line);
        if (!is_at(p, T_STRING)) {
            fail(p, peek(p)->line, "闕引辭");
            ast_free(node);
            return NULL;
        }
        node->str = sangen_xstrdup(peek(p)->sval ? peek(p)->sval : "");
        p->pos++;
        return node;
    }

    if (match(p, T_WRITE)) {
        Node *node = node_new(N_WRITE, tok->line);
        node->expr = parse_expr(p);
        if (!node->expr) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (match(p, T_USE)) {
        Node *node = node_new(N_CALL, tok->line);
        if (!expect_ident(p, &node->name) || !expect(p, T_PROC)) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    fail(p, tok->line, "句首不可識");
    return NULL;
}

static Node *parse_loop(Parser *p)
{
    Node *node;
    const Token *tok = peek(p);

    if (!match(p, T_FAN)) {
        return NULL;
    }

    node = node_new(N_FOR, tok->line);
    if (!expect_var(p, &node->var) || !expect(p, T_FROM)) {
        ast_free(node);
        return NULL;
    }

    node->from = parse_expr(p);
    if (!node->from || !expect(p, T_TO)) {
        ast_free(node);
        return NULL;
    }

    node->to = parse_expr(p);
    if (!node->to) {
        ast_free(node);
        return NULL;
    }

    node->body = parse_block_until(p, T_LOOP_END, T_EOF);
    if (!node->body || !expect(p, T_LOOP_END)) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static Node *parse_while(Parser *p)
{
    Node *node;
    const Token *tok = peek(p);

    if (!match(p, T_WHILE)) {
        return NULL;
    }

    node = node_new(N_WHILE, tok->line);
    node->expr = parse_cond(p);
    if (!node->expr) {
        ast_free(node);
        return NULL;
    }

    node->body = parse_block_until(p, T_LOOP_END, T_EOF);
    if (!node->body || !expect(p, T_LOOP_END)) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static Node *parse_ifchain(Parser *p)
{
    Node *node;
    Node *cond;
    Node *then_block;
    const Token *tok = peek(p);

    if (!match(p, T_IF)) {
        return NULL;
    }

    node = node_new(N_IF, tok->line);
    cond = parse_cond(p);
    if (!cond) {
        ast_free(node);
        return NULL;
    }
    if (!expect(p, T_THEN)) {
        ast_free(cond);
        ast_free(node);
        return NULL;
    }

    then_block = parse_block_until(p, T_ELSE, T_IF_END);
    if (!then_block) {
        ast_free(cond);
        ast_free(node);
        return NULL;
    }
    node_if_append_branch(node, cond, then_block);

    while (match(p, T_ELSE)) {
        if (match(p, T_IF)) {
            cond = parse_cond(p);
            if (!cond) {
                ast_free(node);
                return NULL;
            }
            if (!expect(p, T_THEN)) {
                ast_free(cond);
                ast_free(node);
                return NULL;
            }

            then_block = parse_block_until(p, T_ELSE, T_IF_END);
            if (!then_block) {
                ast_free(cond);
                ast_free(node);
                return NULL;
            }
            node_if_append_branch(node, cond, then_block);
            continue;
        }

        node->els = parse_block_until(p, T_IF_END, T_EOF);
        if (!node->els) {
            ast_free(node);
            return NULL;
        }
        break;
    }

    if (!expect(p, T_IF_END)) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static Node *parse_function(Parser *p)
{
    Node *node;
    const Token *tok = peek(p);
    int saw_param;

    if (!match(p, T_FUNC_START)) {
        return NULL;
    }

    node = node_new(N_FUNC, tok->line);
    if (!expect_ident(p, &node->name) || !expect(p, T_SUBJECT)) {
        ast_free(node);
        return NULL;
    }

    if (!expect(p, T_PROC) || !expect(p, T_ASSERT)) {
        ast_free(node);
        return NULL;
    }

    while (match(p, T_ACCEPT)) {
        saw_param = 0;
        while (is_at(p, T_VAR)) {
            int var;
            int line = peek(p)->line;
            expect_var(p, &var);
            node_func_append_param(node, var, line);
            saw_param = 1;
        }
        if (!saw_param) {
            fail(p, peek(p)->line, "闕受之天干");
            ast_free(node);
            return NULL;
        }
    }

    node->body = parse_block_until(p, T_RETURN, T_EOF);
    if (!node->body || !expect(p, T_RETURN)) {
        ast_free(node);
        return NULL;
    }

    node->expr = parse_expr(p);
    if (!node->expr || !expect(p, T_FUNC_END)) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static Node *parse_item(Parser *p)
{
    Node *node;

    switch (peek(p)->type) {
    case T_MAKE:
    case T_SAY:
    case T_WRITE:
    case T_USE:
        node = parse_simple(p);
        if (!node) {
            return NULL;
        }
        return node;
    case T_FAN:
        return parse_loop(p);
    case T_WHILE:
        return parse_while(p);
    case T_IF:
        return parse_ifchain(p);
    case T_FUNC_START:
        return parse_function(p);
    default:
        fail(p, peek(p)->line, "句首不可識");
        return NULL;
    }
}

Node *parse_program(const Token *tokens, int count, char **error)
{
    Parser p;
    Node *program;

    p.tokens = tokens;
    p.count = count;
    p.pos = 0;
    p.error = NULL;

    program = parse_block_until(&p, T_EOF, T_EOF);
    if (program && !expect(&p, T_EOF)) {
        ast_free(program);
        program = NULL;
    }

    if (p.error) {
        *error = p.error;
        ast_free(program);
        return NULL;
    }

    *error = NULL;
    return program;
}
