#include "surface.h"

#include <stdlib.h>
#include <string.h>

#include "message.h"
#include "support.h"

static int is_han(const Token *token)
{
    return token && token->type == T_HAN;
}

static const char *split_words[] = {
    "大於", "小於", "等於", "不然", "否則", "無餘", "有餘",
    "術畢", "各行", "時復行", "乃得", "而已矣"
};

static int is_split_word(const Token *before, const Token *after)
{
    int i;

    if (!is_han(before) || !is_han(after) || !before->sval || !after->sval) {
        return 0;
    }

    for (i = 0; i < (int)(sizeof(split_words) / sizeof(split_words[0])); i++) {
        if (strlen(split_words[i]) == strlen(before->sval) + strlen(after->sval) &&
            strncmp(split_words[i], before->sval, strlen(before->sval)) == 0 &&
            strcmp(split_words[i] + strlen(before->sval), after->sval) == 0) {
            return 1;
        }
    }

    return 0;
}

static int name_is_split(const TokenArray *raw, int mark_index)
{
    int before = mark_index - 1;
    int after = mark_index + 1;
    int saw_use = 0;

    while (before >= 0 && raw->data[before].line == raw->data[mark_index].line) {
        if (raw->data[before].type == T_KAERI_RE) {
            before--;
            continue;
        }
        if (is_han(&raw->data[before]) && raw->data[before].sval &&
            strcmp(raw->data[before].sval, "用") == 0) {
            saw_use = 1;
            break;
        }
        if (raw->data[before].type != T_HAN) {
            break;
        }
        before--;
    }

    if (!saw_use) {
        return 0;
    }

    while (after < raw->count && raw->data[after].line == raw->data[mark_index].line) {
        if (raw->data[after].type == T_KAERI_RE) {
            after++;
            continue;
        }
        if (is_han(&raw->data[after]) && raw->data[after].sval &&
            strcmp(raw->data[after].sval, "術") == 0) {
            return 1;
        }
        if (raw->data[after].type != T_HAN) {
            return 0;
        }
        after++;
    }

    return 0;
}

static int mark_fail(const Token *token, const char *message, char **error)
{
    char label[128];

    sangen_pos_label(token->line, token->col, label, sizeof(label));
    *error = sangen_format("%s%s", label, message);
    return 0;
}

static int validate_mark(const TokenArray *raw, int raw_index,
                         const SourceMark *mark, char **error)
{
    int before_index = raw_index - 1;
    int after_index = raw_index + 1;
    const Token *before;
    const Token *after;

    if (before_index >= 0 && raw->data[before_index].type == T_KAERI_RE) {
        return mark_fail(&raw->data[raw_index], "返點重出", error);
    }
    if (before_index < 0) {
        return mark_fail(&raw->data[raw_index], "返點無所承", error);
    }
    if (after_index < raw->count && raw->data[after_index].type == T_KAERI_RE) {
        return 1;
    }
    if (after_index >= raw->count || raw->data[after_index].type == T_EOF) {
        return mark_fail(&raw->data[raw_index], "返點無所先", error);
    }

    before = &raw->data[before_index];
    after = &raw->data[after_index];
    if (before->line != after->line) {
        return mark_fail(&raw->data[raw_index], "返點越句", error);
    }
    if (before->type == T_NUMBER && after->type == T_NUMBER) {
        return mark_fail(&raw->data[raw_index], "返點割數", error);
    }
    if (name_is_split(raw, raw_index)) {
        return mark_fail(&raw->data[raw_index], "返點入術名", error);
    }
    if (is_split_word(before, after)) {
        return mark_fail(&raw->data[raw_index], "返點割辭", error);
    }

    (void)mark;
    return 1;
}

static void append_mark(SurfaceStream *surface, const Token *token,
                        int raw_index, int boundary)
{
    SourceMark mark;

    if (surface->mark_count == surface->mark_capacity) {
        int next = surface->mark_capacity ? surface->mark_capacity * 2 : 8;
        surface->marks = sangen_xrealloc(surface->marks,
                                         sizeof(SourceMark) * (size_t)next);
        surface->mark_capacity = next;
    }

    mark.kind = MARK_KAERI_RE;
    mark.boundary = boundary;
    mark.raw_index = raw_index;
    mark.span.start = token->start;
    mark.span.end = token->end;
    mark.span.line = token->line;
    mark.span.col = token->col;
    mark.spelling = token->spelling;
    surface->marks[surface->mark_count++] = mark;
}

int surface_build(const TokenArray *raw, SurfaceStream *surface, char **error)
{
    int i;
    int boundary = 0;

    memset(surface, 0, sizeof(*surface));
    *error = NULL;

    for (i = 0; i < raw->count; i++) {
        const Token *token = &raw->data[i];

        if (token->type == T_KAERI_RE) {
            SourceMark mark;

            memset(&mark, 0, sizeof(mark));
            mark.kind = MARK_KAERI_RE;
            mark.boundary = boundary;
            mark.raw_index = i;
            mark.span.start = token->start;
            mark.span.end = token->end;
            mark.span.line = token->line;
            mark.span.col = token->col;
            mark.spelling = token->spelling;
            if (!validate_mark(raw, i, &mark, error)) {
                surface_free(surface);
                return 0;
            }
            append_mark(surface, token, i, boundary);
            continue;
        }

        surface->base_tokens.data = sangen_xrealloc(
            surface->base_tokens.data,
            sizeof(Token) * (size_t)(surface->base_tokens.count + 1));
        surface->base_tokens.data[surface->base_tokens.count++] = *token;
        boundary++;
    }

    surface->base_tokens.capacity = surface->base_tokens.count;
    return 1;
}

void surface_free(SurfaceStream *surface)
{
    if (!surface) {
        return;
    }

    free(surface->base_tokens.data);
    free(surface->marks);
    memset(surface, 0, sizeof(*surface));
}

int surface_has_marks(const SurfaceStream *surface)
{
    return surface && surface->mark_count > 0;
}
