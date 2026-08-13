#include "reading.h"

#include <stdlib.h>

#include "message.h"
#include "numeral.h"
#include "support.h"

static int has_mark_at(const SurfaceStream *surface, int boundary)
{
    int i;

    for (i = 0; i < surface->mark_count; i++) {
        if (surface->marks[i].boundary == boundary) {
            return 1;
        }
    }

    return 0;
}

static void print_token(const Token *token, FILE *out)
{
    if (token->type == T_NUMBER) {
        char number[128];
        sangen_number_label(token->lval, number, sizeof(number));
        fputs(number, out);
        return;
    }
    if (token->sval) {
        fputs(token->sval, out);
        return;
    }
    fputs(tok_type_name(token->type), out);
}

int derive_reading_order(const SurfaceStream *surface, ReadingOrder *out,
                         char **error)
{
    int i = 0;

    out->indices = NULL;
    out->count = 0;
    *error = NULL;

    while (i < surface->base_tokens.count &&
           surface->base_tokens.data[i].type != T_EOF) {
        int end = i;
        int j;

        while (has_mark_at(surface, end + 1)) {
            end++;
        }

        out->indices = sangen_xrealloc(
            out->indices, sizeof(int) * (size_t)(out->count + end - i + 1));
        for (j = end; j >= i; j--) {
            out->indices[out->count++] = j;
        }
        i = end + 1;
    }

    return 1;
}

void reading_order_free(ReadingOrder *order)
{
    if (!order) {
        return;
    }
    free(order->indices);
    order->indices = NULL;
    order->count = 0;
}

void reading_order_print(const SurfaceStream *surface,
                         const ReadingOrder *order, FILE *out)
{
    int i;

    fputs("原文", out);
    for (i = 0; i < surface->base_tokens.count; i++) {
        const Token *token = &surface->base_tokens.data[i];
        if (token->type == T_EOF) {
            break;
        }
        fputc(i == 0 ? ' ' : ' ', out);
        print_token(token, out);
    }
    fputc('\n', out);

    fputs("讀順", out);
    for (i = 0; i < order->count; i++) {
        fputc(' ', out);
        print_token(&surface->base_tokens.data[order->indices[i]], out);
    }
    fputc('\n', out);
}
