#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"
#include "support.h"

Node *node_new(NodeKind kind, int line)
{
    Node *node = sangen_xmalloc(sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->line = line;
    return node;
}

void node_block_append(Node *block, Node *stmt)
{
    if (!block || block->kind != N_BLOCK) {
        return;
    }

    block->stmts = sangen_xrealloc(block->stmts,
                                   sizeof(Node *) * (size_t)(block->nstmt + 1));
    block->stmts[block->nstmt++] = stmt;
}

void node_if_append_branch(Node *node, Node *cond, Node *then_block)
{
    if (!node || node->kind != N_IF) {
        return;
    }

    node->conds = sangen_xrealloc(node->conds,
                                  sizeof(Node *) * (size_t)(node->nbranch + 1));
    node->thens = sangen_xrealloc(node->thens,
                                  sizeof(Node *) * (size_t)(node->nbranch + 1));
    node->conds[node->nbranch] = cond;
    node->thens[node->nbranch] = then_block;
    node->nbranch++;
}

void node_func_append_param(Node *node, int var, int line)
{
    if (!node || node->kind != N_FUNC) {
        return;
    }

    node->params = sangen_xrealloc(node->params,
                                   sizeof(int) * (size_t)(node->nparam + 1));
    node->param_lines = sangen_xrealloc(node->param_lines,
                                        sizeof(int) * (size_t)(node->nparam + 1));
    node->params[node->nparam++] = var;
    node->param_lines[node->nparam - 1] = line;
}

const char *node_kind_name(NodeKind kind)
{
    switch (kind) {
    case N_NUM: return "數";
    case N_VAR: return "天干";
    case N_BINEXPR: return "算式";
    case N_DIVIS: return "除餘";
    case N_COMPARE: return "較式";
    case N_ASSIGN: return "定";
    case N_SAY: return "曰";
    case N_WRITE: return "書";
    case N_FOR: return "歷";
    case N_WHILE: return "當";
    case N_IF: return "若";
    case N_FUNC: return "術";
    case N_CALL: return "用術";
    case N_BLOCK: return "章";
    }

    return "?";
}

const char *var_name(int var)
{
    static const char *names[] = {
        "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"
    };

    if (var < 0 || var >= 10) {
        return "?";
    }

    return names[var];
}

const char *op_name(int op)
{
    switch ((TokType)op) {
    case T_SUM: return "和";
    case T_DIFF: return "差";
    case T_PROD: return "積";
    case T_QUOT: return "商";
    case T_REM: return "餘";
    case T_GT: return "過";
    case T_LT: return "不及";
    case T_EQ: return "等";
    default: return "?";
    }
}

static void indent(int n)
{
    int i;

    for (i = 0; i < n; i++) {
        putchar(' ');
    }
}

static void print_node(const Node *node, int depth)
{
    int i;

    if (!node) {
        indent(depth);
        printf("空\n");
        return;
    }

    indent(depth);
    switch (node->kind) {
    case N_NUM:
        {
            char num[128];
            sangen_number_label(node->num, num, sizeof(num));
            printf("數 %s\n", num);
        }
        break;
    case N_VAR:
        printf("天干 %s\n", var_name(node->var));
        break;
    case N_BINEXPR:
        printf("算式 %s\n", op_name(node->op));
        print_node(node->lhs, depth + 2);
        print_node(node->rhs, depth + 2);
        break;
    case N_DIVIS:
        printf("除餘 %s %s\n", var_name(node->dividend),
               node->want_no_rem ? "無餘" : "有餘");
        indent(depth + 2);
        printf("法\n");
        print_node(node->divisor, depth + 4);
        break;
    case N_COMPARE:
        printf("較式 %s\n", op_name(node->op));
        print_node(node->lhs, depth + 2);
        print_node(node->rhs, depth + 2);
        break;
    case N_ASSIGN:
        printf("定 %s\n", var_name(node->var));
        print_node(node->expr, depth + 2);
        break;
    case N_SAY:
        printf("曰辭曰%s辭畢\n", node->str ? node->str : "");
        break;
    case N_WRITE:
        printf("書\n");
        print_node(node->expr, depth + 2);
        break;
    case N_CALL:
        printf("用術 %s\n", node->name ? node->name : "");
        break;
    case N_FOR:
        printf("歷 %s\n", var_name(node->var));
        indent(depth + 2);
        printf("始\n");
        print_node(node->from, depth + 4);
        indent(depth + 2);
        printf("終\n");
        print_node(node->to, depth + 4);
        indent(depth + 2);
        printf("句\n");
        print_node(node->body, depth + 4);
        break;
    case N_WHILE:
        printf("當\n");
        indent(depth + 2);
        printf("辨\n");
        print_node(node->expr, depth + 4);
        indent(depth + 2);
        printf("句\n");
        print_node(node->body, depth + 4);
        break;
    case N_IF:
        printf("若\n");
        for (i = 0; i < node->nbranch; i++) {
            char num[128];
            sangen_number_label(i + 1, num, sizeof(num));
            indent(depth + 2);
            printf("第%s辨\n", num);
            print_node(node->conds[i], depth + 4);
            indent(depth + 2);
            printf("第%s則\n", num);
            print_node(node->thens[i], depth + 4);
        }
        if (node->els) {
            indent(depth + 2);
            printf("不然\n");
            print_node(node->els, depth + 4);
        }
        break;
    case N_FUNC:
        printf("術 %s", node->name ? node->name : "");
        if (node->nparam > 0) {
            printf(" 受");
            for (i = 0; i < node->nparam; i++) {
                printf(" %s", var_name(node->params[i]));
            }
        }
        printf("\n");
        indent(depth + 2);
        printf("句\n");
        print_node(node->body, depth + 4);
        indent(depth + 2);
        printf("答\n");
        print_node(node->expr, depth + 4);
        break;
    case N_BLOCK:
        {
            char num[128];
            sangen_number_label(node->nstmt, num, sizeof(num));
            printf("章 %s\n", num);
        }
        for (i = 0; i < node->nstmt; i++) {
            print_node(node->stmts[i], depth + 2);
        }
        break;
    }
}

void ast_print(const Node *node)
{
    print_node(node, 0);
}

void ast_free(Node *node)
{
    int i;

    if (!node) {
        return;
    }

    ast_free(node->lhs);
    ast_free(node->rhs);
    ast_free(node->divisor);
    ast_free(node->expr);
    ast_free(node->from);
    ast_free(node->to);
    ast_free(node->body);

    for (i = 0; i < node->nbranch; i++) {
        ast_free(node->conds[i]);
        ast_free(node->thens[i]);
    }
    free(node->conds);
    free(node->thens);
    ast_free(node->els);

    for (i = 0; i < node->nstmt; i++) {
        ast_free(node->stmts[i]);
    }
    free(node->stmts);
    free(node->str);
    free(node->name);
    free(node->params);
    free(node->param_lines);
    free(node);
}
