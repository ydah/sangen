#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "backend_c.h"
#include "check.h"
#include "interp.h"
#include "lexer.h"
#include "message.h"
#include "parser.h"
#include "reading.h"
#include "support.h"
#include "surface.h"
#include "utf8.h"

typedef enum {
    LINT_ALL,
    LINT_GRAMMAR,
    LINT_STYLE,
    LINT_GLYPH,
    LINT_MARK
} LintMode;

static void usage(FILE *out)
{
    fputs("用曰 sangen 源.kbn [--字碼] [--詞] [--文樹] [--讀順] [--譯=c] [--校[=文法|文體|字體|訓點]] [--正格] [--整[=表記]]\n", out);
}

static int has_kbn_suffix(const char *path)
{
    size_t n = strlen(path);

    return n >= 4 && strcmp(path + n - 4, ".kbn") == 0;
}

static int read_file(const char *path, unsigned char **buf, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    size_t cap = 8192;
    size_t used = 0;

    if (!fp) {
        fprintf(stderr, "源不可讀 %s\n", path);
        return 0;
    }

    *buf = malloc(cap + 1);
    if (!*buf) {
        fprintf(stderr, "所藏不足\n");
        fclose(fp);
        return 0;
    }

    while (!feof(fp)) {
        size_t got;

        if (used == cap) {
            unsigned char *next;

            if (cap > ((size_t)-1) / 2 - 1) {
                fprintf(stderr, "源太大\n");
                free(*buf);
                fclose(fp);
                return 0;
            }
            cap *= 2;
            next = realloc(*buf, cap + 1);
            if (!next) {
                fprintf(stderr, "所藏不足\n");
                free(*buf);
                fclose(fp);
                return 0;
            }
            *buf = next;
        }

        got = fread(*buf + used, 1, cap - used, fp);
        used += got;
        if (ferror(fp)) {
            fprintf(stderr, "源不可讀 %s\n", path);
            free(*buf);
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);

    (*buf)[used] = '\0';
    *len = used;
    return 1;
}

static int print_codepoints(const unsigned char *source, size_t len)
{
    size_t pos = 0;
    int line = 1;

    while (pos < len) {
        uint32_t cp;
        int valid;
        size_t step = utf8_next_checked(source, len, pos, &cp, &valid);

        if (step == 0) {
            break;
        }
        if (!valid) {
            char label[128];
            sangen_line_label(line, label, sizeof(label));
            fprintf(stderr, "%s萬國碼不成\n", label);
            return 0;
        }

        {
            char label[128];
            sangen_line_label(line, label, sizeof(label));
            printf("%s  字碼 U+%04X\n", label, cp);
        }
        if (cp == '\n') {
            line++;
        }
        pos += step;
    }

    return 1;
}

static int token_cp(const Token *tok, uint32_t *cp)
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

static int word_len_at(const TokenArray *tokens, int pos, const char *word)
{
    size_t wpos = 0;
    size_t wlen = strlen(word);
    int n = 0;
    int line;

    if (pos < 0 || pos >= tokens->count) {
        return 0;
    }
    line = tokens->data[pos].line;

    while (wpos < wlen) {
        uint32_t want;
        uint32_t got;
        size_t step = utf8_next((const unsigned char *)word, wlen, wpos, &want);

        if (pos + n >= tokens->count || tokens->data[pos + n].line != line ||
            step == 0 ||
            !token_cp(&tokens->data[pos + n], &got) ||
            got != want) {
            return 0;
        }

        wpos += step;
        n++;
    }

    return n;
}

static int is_word_at(const TokenArray *tokens, int pos, const char *word)
{
    return word_len_at(tokens, pos, word) > 0;
}

static int with_eq_form(const TokenArray *tokens, int pos)
{
    int i;

    for (i = pos - 1; i >= 0 && tokens->data[i].type != T_EOF &&
         tokens->data[i].line == tokens->data[pos].line; i--) {
        if (is_word_at(tokens, i, "與")) {
            return 1;
        }
        if (is_word_at(tokens, i, "若") || is_word_at(tokens, i, "則") ||
            is_word_at(tokens, i, "不然") || is_word_at(tokens, i, "否則")) {
            return 0;
        }
    }

    return 0;
}

static int same_han_token(const Token *lhs, const Token *rhs)
{
    return lhs->type == T_HAN && rhs->type == T_HAN &&
           lhs->sval && rhs->sval && strcmp(lhs->sval, rhs->sval) == 0;
}

static void mark_proc_name_tokens(const TokenArray *tokens,
                                  unsigned char *proc_names,
                                  unsigned char *proc_calls)
{
    int decl;

    for (decl = 0; decl < tokens->count; decl++) {
        int end;
        int name_len;
        int call;

        if (!is_word_at(tokens, decl, "夫")) {
            continue;
        }
        end = decl + 1;
        while (end < tokens->count &&
               tokens->data[end].line == tokens->data[decl].line &&
               !is_word_at(tokens, end, "者")) {
            end++;
        }
        if (end == decl + 1 || !is_word_at(tokens, end, "者") ||
            !is_word_at(tokens, end + 1, "術") ||
            !is_word_at(tokens, end + 2, "也")) {
            continue;
        }
        for (call = decl + 1; call < end; call++) {
            proc_names[call] = 1;
        }

        name_len = end - decl - 1;
        for (call = 0; call + name_len < tokens->count; call++) {
            int j;

            for (j = 0; j < name_len; j++) {
                if (!same_han_token(&tokens->data[call + j],
                                    &tokens->data[decl + 1 + j])) {
                    break;
                }
            }
            if (j == name_len &&
                is_word_at(tokens, call + name_len, "術")) {
                int call_end = call + name_len;

                for (j = 0; j < name_len; j++) {
                    proc_names[call + j] = 1;
                }
                proc_calls[call_end] |= 1;
                if (call > 0 && is_word_at(tokens, call - 1, "用")) {
                    proc_calls[call_end] |= 2;
                }
            }
        }
    }
}

static int call_has_use_prefix(const TokenArray *tokens,
                               const unsigned char *proc_calls, int proc_pos)
{
    int i;

    if (proc_pos > 0 && is_word_at(tokens, proc_pos - 1, "者")) {
        return 1;
    }
    if (is_word_at(tokens, proc_pos, "術畢")) {
        return 1;
    }

    if (proc_calls[proc_pos] & 1) {
        return (proc_calls[proc_pos] & 2) != 0;
    }

    for (i = proc_pos - 1; i >= 0 &&
         tokens->data[i].line == tokens->data[proc_pos].line; i--) {
        if (is_word_at(tokens, i, "術")) {
            return 0;
        }
        if (is_word_at(tokens, i, "用")) {
            return 1;
        }
    }
    return 0;
}

static int report_kanbun_issue(FILE *out, const Token *tok, const char *message)
{
    char label[128];

    sangen_pos_label(tok->line, tok->col, label, sizeof(label));
    fprintf(out, "%s%s\n", label, message);
    return 1;
}

static int lint_kanbun(const TokenArray *tokens, FILE *out, LintMode mode)
{
    int issues = 0;
    int i;
    unsigned char *proc_names = sangen_xmalloc((size_t)tokens->count);
    unsigned char *proc_calls = sangen_xmalloc((size_t)tokens->count);

    memset(proc_names, 0, (size_t)tokens->count);
    memset(proc_calls, 0, (size_t)tokens->count);
    mark_proc_name_tokens(tokens, proc_names, proc_calls);

    for (i = 0; i < tokens->count && tokens->data[i].type != T_EOF; i++) {
        if (tokens->data[i].type == T_KAERI_RE) {
            if ((mode == LINT_ALL || mode == LINT_MARK) &&
                tokens->data[i].spelling == 0x30EC) {
                issues += report_kanbun_issue(
                    out, &tokens->data[i], "此乃片假名 宜用返點「㆑」");
            }
            continue;
        }

        if (proc_names[i]) {
            continue;
        }

        if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
            is_word_at(tokens, i, "使")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「令」");
        } else if ((mode == LINT_ALL || mode == LINT_GLYPH) &&
                   is_word_at(tokens, i, "為")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「爲」");
        } else if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
                   is_word_at(tokens, i, "方")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「當」");
        } else if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
                   is_word_at(tokens, i, "否則")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「不然」");
        } else if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
                   is_word_at(tokens, i, "答")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「歸」");
        } else if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
                   is_word_at(tokens, i, "而已矣")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「已矣」");
        } else if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
                   is_word_at(tokens, i, "術畢")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「已矣」");
        } else if ((mode == LINT_ALL || mode == LINT_STYLE) &&
                   is_word_at(tokens, i, "過")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「大於」");
        } else if ((mode == LINT_ALL || mode == LINT_STYLE) &&
                   is_word_at(tokens, i, "不及")) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「小於」");
        } else if ((mode == LINT_ALL || mode == LINT_STYLE) &&
                   is_word_at(tokens, i, "等") &&
                   !is_word_at(tokens, i + 1, "於") &&
                   !with_eq_form(tokens, i)) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜曰「等於」");
        } else if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
                   is_word_at(tokens, i, "行") &&
                   !(i > 0 && (is_word_at(tokens, i - 1, "各行") ||
                               is_word_at(tokens, i - 1, "復行")))) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此非正格 宜冠「用」而省「行」");
        }

        if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
            (is_word_at(tokens, i, "無") || is_word_at(tokens, i, "有")) &&
            is_word_at(tokens, i + 1, "餘") &&
            !(i > 0 && is_word_at(tokens, i - 1, "而"))) {
            issues += report_kanbun_issue(out, &tokens->data[i],
                                           "此處宜加「而」");
        }

        if ((mode == LINT_ALL || mode == LINT_GRAMMAR) &&
            is_word_at(tokens, i, "術") &&
            !call_has_use_prefix(tokens, proc_calls, i)) {
            issues += report_kanbun_issue(out, &tokens->data[i > 0 ? i - 1 : i],
                                           "術之用未明 宜冠「用」");
        }
    }

    free(proc_names);
    free(proc_calls);
    return issues;
}

static void format_indent(FILE *out, int depth)
{
    int i;

    for (i = 0; i < depth; i++) {
        fputs("　", out);
    }
}

static void format_expr(FILE *out, const Node *node);

static int format_expr_needs_group(const Node *node)
{
    return node && (node->kind == N_BINEXPR ||
                    (node->kind == N_CALL && node->nargs > 0));
}

static void format_grouped_expr(FILE *out, const Node *node)
{
    int grouped = format_expr_needs_group(node);

    if (grouped) {
        fputs("夫", out);
    }
    format_expr(out, node);
    if (grouped) {
        fputs("者", out);
    }
}

static void format_string(FILE *out, const char *s)
{
    const char *p = s ? s : "";

    fputs("辭曰", out);
    while (*p) {
        if (strncmp(p, "辭畢", strlen("辭畢")) == 0) {
            fputs("辭畢辭畢", out);
            p += strlen("辭畢");
            continue;
        }
        fputc((unsigned char)*p, out);
        p++;
    }
    fputs("辭畢", out);
}

static void format_expr(FILE *out, const Node *node)
{
    int i;

    if (!node) {
        return;
    }

    switch (node->kind) {
    case N_NUM:
        {
            char num[128];
            sangen_number_label(node->num, num, sizeof(num));
            fputs(num, out);
        }
        break;
    case N_VAR:
        fputs(var_name(node->var), out);
        break;
    case N_CALL:
        fprintf(out, "用%s術", node->name ? node->name : "");
        for (i = 0; i < node->nargs; i++) {
            fputs(i == 0 ? "以" : "及", out);
            if (node->args[i]->kind == N_CALL && node->args[i]->nargs > 0) {
                fputs("夫", out);
                format_expr(out, node->args[i]);
                fputs("者", out);
            } else {
                format_expr(out, node->args[i]);
            }
        }
        break;
    case N_BINEXPR:
        format_grouped_expr(out, node->lhs);
        fputs("與", out);
        format_grouped_expr(out, node->rhs);
        fputs("之", out);
        fputs(op_name(node->op), out);
        break;
    case N_COMPARE:
        format_expr(out, node->lhs);
        if (node->op == T_GT) {
            fputs("大於", out);
        } else if (node->op == T_LT) {
            fputs("小於", out);
        } else {
            fputs("等於", out);
        }
        format_expr(out, node->rhs);
        break;
    case N_DIVIS:
        fputs("以", out);
        format_expr(out, node->divisor);
        fputs("除", out);
        if (node->dividend_expr) {
            format_expr(out, node->dividend_expr);
        } else {
            fputs(var_name(node->dividend), out);
        }
        fprintf(out, "而%s餘", node->want_no_rem ? "無" : "有");
        break;
    default:
        break;
    }
}

static void format_cond(FILE *out, const Node *node)
{
    if (!node || node->kind != N_BINEXPR ||
        (node->op != T_DIFF && node->op != T_QUOT && node->op != T_REM)) {
        format_expr(out, node);
        return;
    }

    fputs("以", out);
    format_grouped_expr(out, node->rhs);
    if (node->op == T_DIFF) {
        fputs("減", out);
    } else {
        fputs("爲法除", out);
    }
    format_grouped_expr(out, node->lhs);
    fputs("所得之", out);
    fputs(op_name(node->op), out);
}

static void format_block(FILE *out, const Node *block, int depth);

static int block_is_empty(const Node *block)
{
    return block && block->kind == N_BLOCK && block->nstmt == 0;
}

static void format_stmt(FILE *out, const Node *node, int depth)
{
    int i;

    if (!node) {
        return;
    }

    switch (node->kind) {
    case N_ASSIGN:
        format_indent(out, depth);
        fputs("置", out);
        format_expr(out, node->expr);
        fprintf(out, "於%s", var_name(node->var));
        fputs("\n", out);
        break;
    case N_SAY:
        format_indent(out, depth);
        fputs("曰", out);
        format_string(out, node->str);
        fputs("\n", out);
        break;
    case N_WRITE:
        format_indent(out, depth);
        fputs("書", out);
        format_expr(out, node->expr);
        fputs("\n", out);
        break;
    case N_CALL:
        format_indent(out, depth);
        format_expr(out, node);
        fputs("\n", out);
        break;
    case N_FOR:
        format_indent(out, depth);
        fprintf(out, "凡%s自", var_name(node->var));
        format_expr(out, node->from);
        fputs("至", out);
        format_expr(out, node->to);
        fputs("者各行\n", out);
        format_block(out, node->body, depth + 1);
        break;
    case N_WHILE:
        format_indent(out, depth);
        fputs("當", out);
        format_cond(out, node->expr);
        fputs("時復行\n", out);
        format_block(out, node->body, depth + 1);
        break;
    case N_IF:
        {
            int needs_end = node->els && block_is_empty(node->els);

            for (i = 0; i < node->nbranch; i++) {
                needs_end = needs_end || block_is_empty(node->thens[i]);
                format_indent(out, depth);
                fputs(i == 0 ? "若" : "不然若", out);
                format_cond(out, node->conds[i]);
                fputs("則\n", out);
                format_block(out, node->thens[i], depth + 1);
            }
            if (node->els) {
                format_indent(out, depth);
                fputs("不然\n", out);
                format_block(out, node->els, depth + 1);
            }
            if (needs_end) {
                format_indent(out, depth);
                fputs("已矣\n", out);
            }
        }
        break;
    case N_FUNC:
        format_indent(out, depth);
        fprintf(out, "夫%s者術也\n", node->name ? node->name : "");
        if (node->nparam > 0) {
            format_indent(out, depth + 1);
            fputs("受", out);
            for (i = 0; i < node->nparam; i++) {
                if (i > 0) {
                    fputs("及", out);
                }
                fputs(var_name(node->params[i]), out);
            }
            fputs("\n", out);
        }
        format_block(out, node->body, depth + 1);
        format_indent(out, depth + 1);
        fputs("乃得", out);
        format_expr(out, node->expr);
        fputs("\n", out);
        break;
    case N_BLOCK:
        format_block(out, node, depth);
        break;
    default:
        break;
    }
}

static void format_block(FILE *out, const Node *block, int depth)
{
    int i;

    if (!block || block->kind != N_BLOCK) {
        format_stmt(out, block, depth);
        return;
    }

    for (i = 0; i < block->nstmt; i++) {
        format_stmt(out, block->stmts[i], depth);
    }
}

int main(int argc, char **argv)
{
    const char *path;
    unsigned char *source = NULL;
    size_t len = 0;
    TokenArray tokens;
    SurfaceStream surface;
    Node *program = NULL;
    Env env;
    char *error = NULL;
    int show_ast = 0;
    int show_tokens = 0;
    int show_codepoints = 0;
    int backend_c = 0;
    int strict_kanbun = 0;
    int lint_kanbun_mode = 0;
    LintMode lint_mode = LINT_ALL;
    int rewrite_kanbun_mode = 0;
    int surface_rewrite_mode = 0;
    int show_reading = 0;
    int i;
    int status = EXIT_FAILURE;

    if (argc < 2) {
        usage(stderr);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "--助") == 0) {
        usage(stdout);
        return EXIT_SUCCESS;
    }

    path = argv[1];
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--文樹") == 0) {
            show_ast = 1;
        } else if (strcmp(argv[i], "--詞") == 0) {
            show_tokens = 1;
        } else if (strcmp(argv[i], "--字碼") == 0) {
            show_codepoints = 1;
        } else if (strcmp(argv[i], "--讀順") == 0) {
            show_reading = 1;
        } else if (strcmp(argv[i], "--譯=c") == 0) {
            backend_c = 1;
        } else if (strcmp(argv[i], "--正格") == 0 ||
                   strcmp(argv[i], "--strict-kanbun") == 0) {
            strict_kanbun = 1;
        } else if (strcmp(argv[i], "--校") == 0 ||
                   strcmp(argv[i], "--lint-kanbun") == 0) {
            lint_kanbun_mode = 1;
            lint_mode = LINT_ALL;
        } else if (strcmp(argv[i], "--校=文法") == 0) {
            lint_kanbun_mode = 1;
            lint_mode = LINT_GRAMMAR;
        } else if (strcmp(argv[i], "--校=文體") == 0) {
            lint_kanbun_mode = 1;
            lint_mode = LINT_STYLE;
        } else if (strcmp(argv[i], "--校=字體") == 0) {
            lint_kanbun_mode = 1;
            lint_mode = LINT_GLYPH;
        } else if (strcmp(argv[i], "--校=訓點") == 0) {
            lint_kanbun_mode = 1;
            lint_mode = LINT_MARK;
        } else if (strcmp(argv[i], "--整") == 0 ||
                   strcmp(argv[i], "--rewrite-kanbun") == 0) {
            rewrite_kanbun_mode = 1;
        } else if (strcmp(argv[i], "--整=表記") == 0) {
            surface_rewrite_mode = 1;
        } else {
            fprintf(stderr, "不識此選 %s\n", argv[i]);
            usage(stderr);
            return EXIT_FAILURE;
        }
    }

    if ((show_ast ? 1 : 0) + (show_tokens ? 1 : 0) +
        (show_codepoints ? 1 : 0) + (show_reading ? 1 : 0) + (backend_c ? 1 : 0) +
        (lint_kanbun_mode ? 1 : 0) + (rewrite_kanbun_mode ? 1 : 0) +
        (surface_rewrite_mode ? 1 : 0) > 1) {
        fprintf(stderr, "諸選不可並用\n");
        return EXIT_FAILURE;
    }

    if (strict_kanbun && (show_ast || show_tokens || show_codepoints || show_reading ||
                          backend_c || lint_kanbun_mode || rewrite_kanbun_mode ||
                          surface_rewrite_mode)) {
        fprintf(stderr, "諸選不可兼用\n");
        return EXIT_FAILURE;
    }

    if (!has_kbn_suffix(path)) {
        fprintf(stderr, "源名不合\n");
        return EXIT_FAILURE;
    }

    if (!read_file(path, &source, &len)) {
        return EXIT_FAILURE;
    }

    if (show_codepoints) {
        if (!print_codepoints(source, len)) {
            free(source);
            return EXIT_FAILURE;
        }
        free(source);
        return EXIT_SUCCESS;
    }

    if (!lex_source(source, len, &tokens, &error)) {
        fprintf(stderr, "%s\n", error);
        free(error);
        goto cleanup_source;
    }

    if (!surface_build(&tokens, &surface, &error)) {
        fprintf(stderr, "%s\n", error ? error : "返點難識");
        free(error);
        goto cleanup_tokens;
    }

    if (lint_kanbun_mode) {
        lint_kanbun(&tokens, stdout, lint_mode);
        status = EXIT_SUCCESS;
        goto cleanup_tokens;
    }

    if (strict_kanbun && lint_kanbun(&tokens, stderr, LINT_ALL) > 0) {
        goto cleanup_tokens;
    }

    if (show_tokens) {
        tokens_print(&tokens);
        status = EXIT_SUCCESS;
        goto cleanup_surface;
    }

    if (show_reading) {
        ReadingOrder order;

        if (!derive_reading_order(&surface, &order, &error)) {
            fprintf(stderr, "%s\n", error ? error : "讀順難識");
            free(error);
            goto cleanup_surface;
        }
        reading_order_print(&surface, &order, stdout);
        reading_order_free(&order);
        status = EXIT_SUCCESS;
        goto cleanup_surface;
    }

    if (surface_rewrite_mode) {
        surface_print_normalized(&tokens, stdout);
        status = EXIT_SUCCESS;
        goto cleanup_surface;
    }

    program = parse_program(surface.base_tokens.data,
                            surface.base_tokens.count, &error);
    if (!program) {
        fprintf(stderr, "%s\n", error ? error : "文法難識");
        free(error);
        goto cleanup_tokens;
    }

    if (rewrite_kanbun_mode) {
        if (surface_has_marks(&surface)) {
            fprintf(stderr, "返點有り 整文すれば失所す\n");
            goto cleanup_surface;
        }
        if (!check_program(program, &error)) {
            fprintf(stderr, "%s\n", error ? error : "義理不通");
            free(error);
            goto cleanup_tokens;
        }
        format_block(stdout, program, 0);
        status = EXIT_SUCCESS;
        goto cleanup_tokens;
    }

    if (show_ast) {
        ast_print(program);
        surface_print_marks(&surface, stdout);
    }

    if (!show_ast && !check_program(program, &error)) {
        fprintf(stderr, "%s\n", error ? error : "義理不通");
        free(error);
        goto cleanup_tokens;
    }

    if (backend_c) {
        emit_c_program(program, stdout);
    } else if (!show_ast && !show_tokens) {
        env_init(&env);
        if (!interp_program(program, &env)) {
            fprintf(stderr, "%s\n", env.error ? env.error : "中有誤");
            env_free(&env);
            goto cleanup_tokens;
        }
        env_free(&env);
    }

    status = EXIT_SUCCESS;

cleanup_tokens:
    ast_free(program);
cleanup_surface:
    surface_free(&surface);
    tokens_free(&tokens);
cleanup_source:
    free(source);
    return status;
}
