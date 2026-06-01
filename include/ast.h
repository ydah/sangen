#ifndef SANGEN_AST_H
#define SANGEN_AST_H

#include "token.h"

typedef enum {
    N_NUM,
    N_VAR,
    N_BINEXPR,
    N_DIVIS,
    N_COMPARE,
    N_ASSIGN,
    N_SAY,
    N_WRITE,
    N_FOR,
    N_WHILE,
    N_IF,
    N_FUNC,
    N_CALL,
    N_BLOCK
} NodeKind;

typedef struct Node Node;

struct Node {
    NodeKind kind;
    long num;
    int var;
    int op;
    Node *lhs;
    Node *rhs;
    Node *divisor;
    int dividend;
    int want_no_rem;
    char *str;
    char *name;
    Node *expr;
    Node *from;
    Node *to;
    Node *body;
    Node **conds;
    Node **thens;
    int nbranch;
    Node *els;
    Node **stmts;
    int nstmt;
    int *params;
    int *param_lines;
    int nparam;
    int line;
};

Node *node_new(NodeKind kind, int line);
void node_block_append(Node *block, Node *stmt);
void node_if_append_branch(Node *node, Node *cond, Node *then_block);
void node_func_append_param(Node *node, int var, int line);
void ast_print(const Node *node);
void ast_free(Node *node);

const char *node_kind_name(NodeKind kind);
const char *var_name(int var);
const char *op_name(int op);

#endif
