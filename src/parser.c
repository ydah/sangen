#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"
#include "support.h"
#include "utf8.h"

typedef struct {
    const Token *tokens;
    int count;
    int pos;
    char *error;
    int block_line;
    int block_col;
    int indent_stop;
} Parser;

typedef int (*StopFn)(Parser *);

static const Token *token_at(Parser *p, int pos)
{
    if (pos >= p->count) {
        return &p->tokens[p->count - 1];
    }

    return &p->tokens[pos];
}

static const Token *peek(Parser *p)
{
    return token_at(p, p->pos);
}

static const Token *previous(Parser *p)
{
    if (p->pos <= 0) {
        return &p->tokens[0];
    }

    return &p->tokens[p->pos - 1];
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

static int han_cp(const Token *tok, uint32_t *cp)
{
    size_t len;
    size_t step;

    if (tok->type != T_HAN || !tok->sval) {
        return 0;
    }

    len = strlen(tok->sval);
    step = utf8_next((const unsigned char *)tok->sval, len, 0, cp);
    return step > 0 && step == len;
}

static int word_len_at(Parser *p, int pos, const char *word)
{
    size_t wpos = 0;
    size_t wlen = strlen(word);
    int n = 0;

    while (wpos < wlen) {
        uint32_t want;
        uint32_t got;
        size_t step = utf8_next((const unsigned char *)word, wlen, wpos, &want);

        if (step == 0 || !han_cp(token_at(p, pos + n), &got) || got != want) {
            return 0;
        }

        wpos += step;
        n++;
    }

    return n;
}

static int is_word_at(Parser *p, const char *word)
{
    return word_len_at(p, p->pos, word) > 0;
}

static int match_word(Parser *p, const char *word)
{
    int n = word_len_at(p, p->pos, word);

    if (n == 0) {
        return 0;
    }

    p->pos += n;
    return 1;
}

static int match_any_word(Parser *p, const char **words, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (match_word(p, words[i])) {
            return 1;
        }
    }

    return 0;
}

static int expect_word(Parser *p, const char *word, const char *name)
{
    if (match_word(p, word)) {
        return 1;
    }

    return fail(p, peek(p)->line, "闕%s", name);
}

static int expect_any_word(Parser *p, const char **words, int count,
                           const char *name)
{
    if (match_any_word(p, words, count)) {
        return 1;
    }

    return fail(p, peek(p)->line, "闕%s", name);
}

static int var_index_cp(uint32_t cp)
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

static int var_at(Parser *p, int pos, int *var)
{
    uint32_t cp;
    int index;

    if (!han_cp(token_at(p, pos), &cp)) {
        return 0;
    }

    index = var_index_cp(cp);
    if (index < 0) {
        return 0;
    }

    *var = index;
    return 1;
}

static int expect_var(Parser *p, int *var)
{
    if (!var_at(p, p->pos, var)) {
        return fail(p, peek(p)->line, "闕天干");
    }

    p->pos++;
    return 1;
}

static void bytes_append(char **buf, size_t *len, size_t *cap, const char *src)
{
    size_t n = strlen(src);

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

static int expect_ident_until(Parser *p, char **name, const char *stop)
{
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int saw = 0;
    int line = peek(p)->line;

    while (peek(p)->type != T_EOF && peek(p)->line == line &&
           !is_word_at(p, stop)) {
        if (peek(p)->type != T_HAN) {
            free(buf);
            return fail(p, peek(p)->line, "闕術之名");
        }
        bytes_append(&buf, &len, &cap, peek(p)->sval ? peek(p)->sval : "");
        p->pos++;
        saw = 1;
    }

    if (!saw) {
        free(buf);
        return fail(p, peek(p)->line, "闕術之名");
    }

    *name = buf;
    return 1;
}

static int has_ident_until(Parser *p, int pos, const char *stop)
{
    int saw = 0;
    int line = token_at(p, pos)->line;

    while (token_at(p, pos)->type == T_HAN && token_at(p, pos)->line == line) {
        if (word_len_at(p, pos, stop) > 0) {
            return saw;
        }
        pos++;
        saw = 1;
    }

    return 0;
}

static int is_expr_op(TokType type)
{
    return type == T_SUM || type == T_DIFF || type == T_PROD ||
           type == T_QUOT || type == T_REM;
}

static int op_at(Parser *p, int pos)
{
    if (word_len_at(p, pos, "和") > 0) {
        return T_SUM;
    }
    if (word_len_at(p, pos, "差") > 0) {
        return T_DIFF;
    }
    if (word_len_at(p, pos, "積") > 0) {
        return T_PROD;
    }
    if (word_len_at(p, pos, "商") > 0) {
        return T_QUOT;
    }
    if (word_len_at(p, pos, "餘") > 0) {
        return T_REM;
    }

    return 0;
}

static int match_expr_op(Parser *p, int *op)
{
    int got = op_at(p, p->pos);

    if (!is_expr_op((TokType)got)) {
        return 0;
    }

    p->pos++;
    *op = got;
    return 1;
}

static int is_eof_stop(Parser *p)
{
    return peek(p)->type == T_EOF;
}

static int is_else(Parser *p)
{
    return is_word_at(p, "不然") || is_word_at(p, "否則");
}

static int is_if_end(Parser *p)
{
    return is_word_at(p, "已矣") || is_word_at(p, "而已矣") ||
           is_word_at(p, "畢");
}

static int is_loop_end(Parser *p)
{
    return is_word_at(p, "焉") || peek(p)->type == T_EOF;
}

static int is_if_body_stop(Parser *p)
{
    return is_else(p) || is_if_end(p) || peek(p)->type == T_EOF;
}

static int is_return_stop(Parser *p)
{
    return is_word_at(p, "歸") || is_word_at(p, "答") ||
           is_word_at(p, "乃得") ||
           peek(p)->type == T_EOF;
}

static Node *parse_item(Parser *p);
static Node *parse_expr(Parser *p);
static Node *parse_cond(Parser *p);
static Node *parse_primary(Parser *p);

static int at_indent_boundary(Parser *p)
{
    return p->indent_stop && peek(p)->type != T_EOF &&
           peek(p)->line > p->block_line && peek(p)->col <= p->block_col;
}

static Node *parse_block_until(Parser *p, StopFn stop, int line, int col,
                               int indent_stop)
{
    Node *block = node_new(N_BLOCK, peek(p)->line);
    int old_line = p->block_line;
    int old_col = p->block_col;
    int old_indent_stop = p->indent_stop;

    p->block_line = line;
    p->block_col = col;
    p->indent_stop = indent_stop;

    while (!stop(p) && !at_indent_boundary(p)) {
        Node *item = parse_item(p);
        if (!item) {
            p->block_line = old_line;
            p->block_col = old_col;
            p->indent_stop = old_indent_stop;
            ast_free(block);
            return NULL;
        }
        node_block_append(block, item);
    }

    p->block_line = old_line;
    p->block_col = old_col;
    p->indent_stop = old_indent_stop;

    return block;
}

static Node *parse_suite(Parser *p, StopFn stop, int line, int col)
{
    Node *block;

    if (peek(p)->line == line) {
        Node *item = parse_item(p);

        if (!item) {
            return NULL;
        }
        block = node_new(N_BLOCK, item->line);
        node_block_append(block, item);
        return block;
    }

    return parse_block_until(p, stop, line, col,
                             peek(p)->line > line && peek(p)->col > col);
}

static Node *parse_call_atom(Parser *p, int line)
{
    Node *node = node_new(N_CALL, line);

    if (!expect_ident_until(p, &node->name, "術") ||
        !expect_word(p, "術", "術")) {
        ast_free(node);
        return NULL;
    }

    if (match_word(p, "以")) {
        Node *arg = parse_expr(p);

        if (!arg) {
            ast_free(node);
            return NULL;
        }
        node_call_append_arg(node, arg);
        while (match_word(p, "及")) {
            arg = parse_expr(p);
            if (!arg) {
                ast_free(node);
                return NULL;
            }
            node_call_append_arg(node, arg);
        }
    }

    return node;
}

static Node *parse_primary(Parser *p)
{
    const Token *tok = peek(p);
    Node *node;
    int var;

    if (peek(p)->type == T_NUMBER) {
        node = node_new(N_NUM, tok->line);
        node->num = tok->lval;
        p->pos++;
        return node;
    }

    if (match_word(p, "用")) {
        return parse_call_atom(p, tok->line);
    }

    if (match_word(p, "夫")) {
        node = parse_expr(p);
        if (!node || !expect_word(p, "者", "者")) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (has_ident_until(p, p->pos, "術")) {
        return parse_call_atom(p, tok->line);
    }

    if (var_at(p, p->pos, &var)) {
        node = node_new(N_VAR, tok->line);
        node->var = var;
        p->pos++;
        return node;
    }

    fail(p, tok->line, "此處當書數天干或術");
    return NULL;
}

static int has_binary_tail(Parser *p)
{
    Parser probe = *p;
    Node *rhs;

    probe.error = NULL;
    if (match_word(&probe, "與")) {
        rhs = parse_primary(&probe);
    } else {
        rhs = parse_primary(&probe);
    }
    if (!rhs || !match_word(&probe, "之")) {
        ast_free(rhs);
        return 0;
    }
    ast_free(rhs);
    return is_expr_op((TokType)op_at(&probe, probe.pos));
}

static Node *parse_binary_tail(Parser *p, Node *lhs)
{
    Node *rhs;
    Node *node;
    int op;

    match_word(p, "與");
    rhs = parse_primary(p);
    if (!rhs) {
        ast_free(lhs);
        return NULL;
    }

    if (!expect_word(p, "之", "之") || !match_expr_op(p, &op)) {
        if (!p->error) {
            fail(p, peek(p)->line, "闕和差積商餘之一");
        }
        ast_free(lhs);
        ast_free(rhs);
        return NULL;
    }

    node = node_new(N_BINEXPR, lhs->line);
    node->lhs = lhs;
    node->rhs = rhs;
    node->op = op;
    return node;
}

static int is_explicit_arithmetic(Parser *p)
{
    Parser probe = *p;
    Node *first;

    probe.error = NULL;
    if (!match_word(&probe, "以")) {
        return 0;
    }
    first = parse_primary(&probe);
    if (!first) {
        return 0;
    }
    ast_free(first);
    return is_word_at(&probe, "減") || is_word_at(&probe, "爲") ||
           is_word_at(&probe, "為");
}

static Node *parse_explicit_arithmetic(Parser *p)
{
    Node *first;
    Node *second;
    Node *node;
    int op;
    int line = peek(p)->line;

    expect_word(p, "以", "以");
    first = parse_primary(p);
    if (!first) {
        return NULL;
    }

    if (match_word(p, "減")) {
        op = T_DIFF;
    } else {
        if (!match_word(p, "爲") && !match_word(p, "為")) {
            ast_free(first);
            fail(p, peek(p)->line, "闕減若爲法");
            return NULL;
        }
        if (!expect_word(p, "法", "法") || !expect_word(p, "除", "除")) {
            ast_free(first);
            return NULL;
        }
        op = -1;
    }

    second = parse_primary(p);
    if (!second || !expect_word(p, "所得", "所得") ||
        !expect_word(p, "之", "之")) {
        ast_free(first);
        ast_free(second);
        return NULL;
    }

    if (op == T_DIFF) {
        if (!expect_word(p, "差", "差")) {
            ast_free(first);
            ast_free(second);
            return NULL;
        }
    } else if (match_word(p, "商")) {
        op = T_QUOT;
    } else if (match_word(p, "餘")) {
        op = T_REM;
    } else {
        ast_free(first);
        ast_free(second);
        fail(p, peek(p)->line, "闕商若餘");
        return NULL;
    }

    node = node_new(N_BINEXPR, line);
    node->lhs = second;
    node->rhs = first;
    node->op = op;
    node->line = line;
    return node;
}

static Node *parse_expr(Parser *p)
{
    Node *lhs;

    if (is_explicit_arithmetic(p)) {
        return parse_explicit_arithmetic(p);
    }

    lhs = parse_primary(p);
    if (!lhs) {
        return NULL;
    }
    if (peek(p)->line == lhs->line && has_binary_tail(p)) {
        return parse_binary_tail(p, lhs);
    }
    return lhs;
}

static Node *parse_divis(Parser *p)
{
    static const char *rem_words[] = {"無", "有"};
    Node *node;
    Node *divisor;
    Node *dividend_expr;
    int want_no_rem;
    int line = previous(p)->line;

    divisor = parse_expr(p);
    if (!divisor) {
        return NULL;
    }

    if (!expect_word(p, "除", "除")) {
        ast_free(divisor);
        return NULL;
    }

    dividend_expr = parse_expr(p);
    if (!dividend_expr) {
        ast_free(divisor);
        return NULL;
    }

    match_word(p, "而");

    if (match_any_word(p, rem_words, 2)) {
        want_no_rem = word_len_at(p, p->pos - 1, "無") > 0;
    } else {
        fail(p, peek(p)->line, "闕無餘若有餘");
        ast_free(divisor);
        ast_free(dividend_expr);
        return NULL;
    }

    if (!expect_word(p, "餘", "餘")) {
        ast_free(divisor);
        ast_free(dividend_expr);
        return NULL;
    }

    node = node_new(N_DIVIS, line);
    node->divisor = divisor;
    if (dividend_expr->kind == N_VAR) {
        node->dividend = dividend_expr->var;
        ast_free(dividend_expr);
    } else {
        node->dividend_expr = dividend_expr;
    }
    node->want_no_rem = want_no_rem;
    return node;
}

static int match_compare_op(Parser *p, int *op)
{
    if (match_word(p, "大") || match_word(p, "過")) {
        *op = T_GT;
        return 1;
    }
    if (match_word(p, "小") || match_word(p, "不及")) {
        *op = T_LT;
        return 1;
    }
    if (match_word(p, "等")) {
        *op = T_EQ;
        return 1;
    }

    return 0;
}

static Node *parse_compare(Parser *p)
{
    Node *lhs = parse_expr(p);
    Node *rhs;
    Node *node;
    int op;

    if (!lhs) {
        return NULL;
    }

    if (match_word(p, "與")) {
        rhs = parse_expr(p);
        if (!rhs) {
            ast_free(lhs);
            return NULL;
        }
        if (!expect_word(p, "等", "等")) {
            ast_free(lhs);
            ast_free(rhs);
            return NULL;
        }

        node = node_new(N_COMPARE, lhs->line);
        node->lhs = lhs;
        node->rhs = rhs;
        node->op = T_EQ;
        match_word(p, "於");
        return node;
    }

    if (!match_compare_op(p, &op)) {
        fail(p, peek(p)->line, "闕比較之辭");
        ast_free(lhs);
        return NULL;
    }
    match_word(p, "於");

    rhs = parse_expr(p);
    if (!rhs) {
        ast_free(lhs);
        return NULL;
    }

    node = node_new(N_COMPARE, lhs->line);
    node->lhs = lhs;
    node->rhs = rhs;
    node->op = op;
    return node;
}

static Node *parse_cond(Parser *p)
{
    if (is_explicit_arithmetic(p)) {
        return parse_expr(p);
    }
    if (match_word(p, "以")) {
        return parse_divis(p);
    }

    return parse_compare(p);
}

static Node *parse_call_stmt(Parser *p, int line)
{
    return parse_call_atom(p, line);
}

static Node *parse_simple(Parser *p)
{
    static const char *make_words[] = {"令", "使"};
    static const char *be_words[] = {"爲", "為"};
    const Token *tok = peek(p);

    if (match_any_word(p, make_words, 2)) {
        Node *node = node_new(N_ASSIGN, tok->line);
        if (!expect_var(p, &node->var) ||
            !expect_any_word(p, be_words, 2, "爲")) {
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

    if (match_word(p, "置")) {
        Node *node = node_new(N_ASSIGN, tok->line);

        node->expr = parse_expr(p);
        if (!node->expr || !expect_word(p, "於", "於") ||
            !expect_var(p, &node->var)) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (match_word(p, "曰")) {
        Node *node = node_new(N_SAY, tok->line);
        if (peek(p)->type != T_STRING) {
            fail(p, peek(p)->line, "闕引辭");
            ast_free(node);
            return NULL;
        }
        node->str = sangen_xstrdup(peek(p)->sval ? peek(p)->sval : "");
        p->pos++;
        return node;
    }

    if (match_word(p, "書")) {
        Node *node = node_new(N_WRITE, tok->line);
        node->expr = parse_expr(p);
        if (!node->expr) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (match_word(p, "用")) {
        return parse_call_stmt(p, tok->line);
    }

    if (match_word(p, "行")) {
        match_word(p, "用");
        return parse_call_stmt(p, tok->line);
    }

    if (has_ident_until(p, p->pos, "術")) {
        return parse_call_stmt(p, tok->line);
    }

    fail(p, tok->line, "句首不可識");
    return NULL;
}

static Node *parse_loop(Parser *p)
{
    Node *node;
    const Token *tok = peek(p);
    int indented_suite;

    if (!match_word(p, "凡")) {
        return NULL;
    }

    node = node_new(N_FOR, tok->line);
    if (!expect_var(p, &node->var) || !expect_word(p, "自", "自")) {
        ast_free(node);
        return NULL;
    }

    node->from = parse_expr(p);
    if (!node->from || !expect_word(p, "至", "至")) {
        ast_free(node);
        return NULL;
    }

    node->to = parse_expr(p);
    if (!node->to) {
        ast_free(node);
        return NULL;
    }
    match_word(p, "者");
    indented_suite = match_word(p, "各行");

    node->body = indented_suite
        ? parse_suite(p, is_loop_end, tok->line, tok->col)
        : parse_block_until(p, is_loop_end, tok->line, tok->col, 0);
    if (!node->body || (!indented_suite && !expect_word(p, "焉", "焉"))) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static Node *parse_while(Parser *p)
{
    static const char *while_words[] = {"當", "方"};
    Node *node;
    const Token *tok = peek(p);
    int indented_suite;

    if (!match_any_word(p, while_words, 2)) {
        return NULL;
    }

    node = node_new(N_WHILE, tok->line);
    node->expr = parse_cond(p);
    if (!node->expr) {
        ast_free(node);
        return NULL;
    }

    indented_suite = match_word(p, "時復行");
    node->body = indented_suite
        ? parse_suite(p, is_loop_end, tok->line, tok->col)
        : parse_block_until(p, is_loop_end, tok->line, tok->col, 0);
    if (!node->body || (!indented_suite && !expect_word(p, "焉", "焉"))) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static int expect_if_end(Parser *p)
{
    static const char *end_words[] = {"而已矣", "已矣", "畢"};

    return expect_any_word(p, end_words, 3, "已矣");
}

static Node *parse_ifchain(Parser *p)
{
    Node *node;
    Node *cond;
    Node *then_block;
    const Token *tok = peek(p);
    int indented_suite = 0;

    if (!match_word(p, "若")) {
        return NULL;
    }

    node = node_new(N_IF, tok->line);
    cond = parse_cond(p);
    if (!cond) {
        ast_free(node);
        return NULL;
    }
    if (!expect_word(p, "則", "則")) {
        ast_free(cond);
        ast_free(node);
        return NULL;
    }

    if (peek(p)->line > tok->line && peek(p)->col > tok->col) {
        indented_suite = 1;
    }
    then_block = indented_suite
        ? parse_suite(p, is_if_body_stop, tok->line, tok->col)
        : parse_block_until(p, is_if_body_stop, tok->line, tok->col, 0);
    if (!then_block) {
        ast_free(cond);
        ast_free(node);
        return NULL;
    }
    node_if_append_branch(node, cond, then_block);

    while (is_else(p)) {
        static const char *else_words[] = {"不然", "否則"};
        const Token *else_tok = peek(p);
        int branch_suite;

        match_any_word(p, else_words, 2);
        if (match_word(p, "若")) {
            cond = parse_cond(p);
            if (!cond) {
                ast_free(node);
                return NULL;
            }
            if (!expect_word(p, "則", "則")) {
                ast_free(cond);
                ast_free(node);
                return NULL;
            }

            branch_suite = peek(p)->line > else_tok->line &&
                           peek(p)->col > else_tok->col;
            indented_suite = indented_suite || branch_suite;
            then_block = branch_suite
                ? parse_suite(p, is_if_body_stop, else_tok->line, else_tok->col)
                : parse_block_until(p, is_if_body_stop,
                                    else_tok->line, else_tok->col, 0);
            if (!then_block) {
                ast_free(cond);
                ast_free(node);
                return NULL;
            }
            node_if_append_branch(node, cond, then_block);
            continue;
        }

        branch_suite = peek(p)->line > else_tok->line &&
                       peek(p)->col > else_tok->col;
        indented_suite = indented_suite || branch_suite;
        node->els = branch_suite
            ? parse_suite(p, is_if_end, else_tok->line, else_tok->col)
            : parse_block_until(p, is_if_end,
                                else_tok->line, else_tok->col, 0);
        if (!node->els) {
            ast_free(node);
            return NULL;
        }
        break;
    }

    if (indented_suite) {
        if (is_if_end(p)) {
            expect_if_end(p);
        }
    } else if (!expect_if_end(p)) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static int expect_func_end(Parser *p)
{
    static const char *end_words[] = {"而已矣", "已矣", "術畢", "畢"};

    return expect_any_word(p, end_words, 4, "已矣");
}

static Node *parse_function(Parser *p)
{
    static const char *return_words[] = {"歸", "答"};
    Node *node;
    const Token *tok = peek(p);
    int saw_param;
    int explicit_return;

    if (!match_word(p, "夫")) {
        return NULL;
    }

    node = node_new(N_FUNC, tok->line);
    if (!expect_ident_until(p, &node->name, "者") ||
        !expect_word(p, "者", "者")) {
        ast_free(node);
        return NULL;
    }

    if (!expect_word(p, "術", "術") || !expect_word(p, "也", "也")) {
        ast_free(node);
        return NULL;
    }

    while (match_word(p, "受")) {
        saw_param = 0;
        while (peek(p)->type == T_HAN) {
            int var;
            int line = peek(p)->line;

            if (!var_at(p, p->pos, &var)) {
                break;
            }
            expect_var(p, &var);
            node_func_append_param(node, var, line);
            saw_param = 1;
            if (!match_word(p, "及")) {
                continue;
            }
            if (!expect_var(p, &var)) {
                ast_free(node);
                return NULL;
            }
            node_func_append_param(node, var, line);
        }
        if (!saw_param) {
            fail(p, peek(p)->line, "闕受之天干");
            ast_free(node);
            return NULL;
        }
    }

    node->body = parse_block_until(
        p, is_return_stop, tok->line, tok->col,
        peek(p)->line > tok->line && peek(p)->col > tok->col);
    if (!node->body) {
        ast_free(node);
        return NULL;
    }

    explicit_return = match_word(p, "乃得");
    if (!explicit_return && !expect_any_word(p, return_words, 2, "歸")) {
        ast_free(node);
        return NULL;
    }
    node->expr = parse_expr(p);
    if (!node->expr || (!explicit_return && !expect_func_end(p))) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static Node *parse_item(Parser *p)
{
    if (is_word_at(p, "凡")) {
        return parse_loop(p);
    }
    if (is_word_at(p, "當") || is_word_at(p, "方")) {
        return parse_while(p);
    }
    if (is_word_at(p, "若")) {
        return parse_ifchain(p);
    }
    if (is_word_at(p, "夫")) {
        return parse_function(p);
    }
    if (is_word_at(p, "令") || is_word_at(p, "使") || is_word_at(p, "置") ||
        is_word_at(p, "曰") || is_word_at(p, "書") ||
        is_word_at(p, "用") || is_word_at(p, "行") ||
        has_ident_until(p, p->pos, "術")) {
        return parse_simple(p);
    }

    fail(p, peek(p)->line, "句首不可識");
    return NULL;
}

Node *parse_program(const Token *tokens, int count, char **error)
{
    Parser p;
    Node *program;

    p.tokens = tokens;
    p.count = count;
    p.pos = 0;
    p.error = NULL;

    program = parse_block_until(&p, is_eof_stop, 0, 0, 0);
    if (program && peek(&p)->type != T_EOF) {
        ast_free(program);
        program = NULL;
        fail(&p, peek(&p)->line, "闕文終");
    }

    if (p.error) {
        *error = p.error;
        ast_free(program);
        return NULL;
    }

    *error = NULL;
    return program;
}
