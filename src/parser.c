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

    while (peek(p)->type != T_EOF && !is_word_at(p, stop)) {
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

    while (token_at(p, pos)->type == T_HAN) {
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
           peek(p)->type == T_EOF;
}

static Node *parse_item(Parser *p);
static Node *parse_expr(Parser *p);
static Node *parse_cond(Parser *p);

static Node *parse_block_until(Parser *p, StopFn stop)
{
    Node *block = node_new(N_BLOCK, peek(p)->line);

    while (!stop(p)) {
        Node *item = parse_item(p);
        if (!item) {
            ast_free(block);
            return NULL;
        }
        node_block_append(block, item);
    }

    return block;
}

static int atom_end_at(Parser *p, int pos)
{
    int ignored;

    if (token_at(p, pos)->type == T_NUMBER) {
        return pos + 1;
    }

    if (word_len_at(p, pos, "用") > 0) {
        int start = pos + word_len_at(p, pos, "用");

        while (token_at(p, start)->type == T_HAN) {
            if (word_len_at(p, start, "術") > 0) {
                return start + word_len_at(p, start, "術");
            }
            start++;
        }
        return -1;
    }

    if (has_ident_until(p, pos, "術")) {
        int end = pos;

        while (token_at(p, end)->type == T_HAN &&
               word_len_at(p, end, "術") == 0) {
            end++;
        }
        return end + word_len_at(p, end, "術");
    }

    if (var_at(p, pos, &ignored)) {
        return pos + 1;
    }

    return -1;
}

static int has_expr_tail(Parser *p, int *has_with)
{
    int start = p->pos;
    int rhs_start = start;
    int rhs_end;

    *has_with = 0;
    if (word_len_at(p, start, "與") > 0) {
        *has_with = 1;
        rhs_start = start + word_len_at(p, start, "與");
    }

    rhs_end = atom_end_at(p, rhs_start);
    return rhs_end >= 0 &&
           word_len_at(p, rhs_end, "之") > 0 &&
           is_expr_op((TokType)op_at(p, rhs_end + word_len_at(p, rhs_end, "之")));
}

static Node *parse_call_atom(Parser *p, int line)
{
    Node *node = node_new(N_CALL, line);

    if (!expect_ident_until(p, &node->name, "術") ||
        !expect_word(p, "術", "術")) {
        ast_free(node);
        return NULL;
    }

    return node;
}

static Node *parse_atom(Parser *p)
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

static Node *parse_expr(Parser *p)
{
    Node *lhs = parse_atom(p);
    Node *rhs;
    Node *node;
    int has_with;
    int op;

    if (!lhs) {
        return NULL;
    }

    if (!has_expr_tail(p, &has_with)) {
        return lhs;
    }
    if (has_with) {
        match_word(p, "與");
    }

    rhs = parse_atom(p);
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

static Node *parse_divis(Parser *p)
{
    static const char *rem_words[] = {"無", "有"};
    Node *node;
    Node *divisor;
    int dividend;
    int want_no_rem;
    int line = previous(p)->line;

    divisor = parse_expr(p);
    if (!divisor) {
        return NULL;
    }

    if (!expect_word(p, "除", "除") || !expect_var(p, &dividend)) {
        ast_free(divisor);
        return NULL;
    }

    match_word(p, "而");

    if (match_any_word(p, rem_words, 2)) {
        want_no_rem = word_len_at(p, p->pos - 1, "無") > 0;
    } else {
        fail(p, peek(p)->line, "闕無餘若有餘");
        ast_free(divisor);
        return NULL;
    }

    if (!expect_word(p, "餘", "餘")) {
        ast_free(divisor);
        return NULL;
    }

    node = node_new(N_DIVIS, line);
    node->divisor = divisor;
    node->dividend = dividend;
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

    node->body = parse_block_until(p, is_loop_end);
    if (!node->body || !expect_word(p, "焉", "焉")) {
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

    if (!match_any_word(p, while_words, 2)) {
        return NULL;
    }

    node = node_new(N_WHILE, tok->line);
    node->expr = parse_cond(p);
    if (!node->expr) {
        ast_free(node);
        return NULL;
    }

    node->body = parse_block_until(p, is_loop_end);
    if (!node->body || !expect_word(p, "焉", "焉")) {
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

    then_block = parse_block_until(p, is_if_body_stop);
    if (!then_block) {
        ast_free(cond);
        ast_free(node);
        return NULL;
    }
    node_if_append_branch(node, cond, then_block);

    while (is_else(p)) {
        static const char *else_words[] = {"不然", "否則"};

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

            then_block = parse_block_until(p, is_if_body_stop);
            if (!then_block) {
                ast_free(cond);
                ast_free(node);
                return NULL;
            }
            node_if_append_branch(node, cond, then_block);
            continue;
        }

        node->els = parse_block_until(p, is_if_end);
        if (!node->els) {
            ast_free(node);
            return NULL;
        }
        break;
    }

    if (!expect_if_end(p)) {
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
        }
        if (!saw_param) {
            fail(p, peek(p)->line, "闕受之天干");
            ast_free(node);
            return NULL;
        }
    }

    node->body = parse_block_until(p, is_return_stop);
    if (!node->body || !expect_any_word(p, return_words, 2, "歸")) {
        ast_free(node);
        return NULL;
    }

    node->expr = parse_expr(p);
    if (!node->expr || !expect_func_end(p)) {
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
    if (is_word_at(p, "令") || is_word_at(p, "使") ||
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

    program = parse_block_until(&p, is_eof_stop);
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
