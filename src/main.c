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
#include "utf8.h"

static void usage(FILE *out)
{
    fputs("用曰 sangen 源.kbn [--字碼] [--詞] [--文樹] [--譯=c]\n", out);
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

int main(int argc, char **argv)
{
    const char *path;
    unsigned char *source = NULL;
    size_t len = 0;
    TokenArray tokens;
    Node *program = NULL;
    Env env;
    char *error = NULL;
    int show_ast = 0;
    int show_tokens = 0;
    int show_codepoints = 0;
    int backend_c = 0;
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
        } else if (strcmp(argv[i], "--譯=c") == 0) {
            backend_c = 1;
        } else {
            fprintf(stderr, "不識此選 %s\n", argv[i]);
            usage(stderr);
            return EXIT_FAILURE;
        }
    }

    if ((show_ast ? 1 : 0) + (show_tokens ? 1 : 0) +
        (show_codepoints ? 1 : 0) + (backend_c ? 1 : 0) > 1) {
        fprintf(stderr, "諸選不可並用\n");
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

    if (show_tokens) {
        tokens_print(&tokens);
        status = EXIT_SUCCESS;
        goto cleanup_tokens;
    }

    program = parse_program(tokens.data, tokens.count, &error);
    if (!program) {
        fprintf(stderr, "%s\n", error ? error : "文法難識");
        free(error);
        goto cleanup_tokens;
    }

    if (show_ast) {
        ast_print(program);
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
    tokens_free(&tokens);
cleanup_source:
    free(source);
    return status;
}
