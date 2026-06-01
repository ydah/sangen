#include "backend_c.h"

#include <stdio.h>
#include <string.h>

#include "message.h"

typedef struct {
    const Node *program;
} EmitCtx;

static void indent(FILE *out, int depth)
{
    int i;

    for (i = 0; i < depth; i++) {
        fputs("    ", out);
    }
}

static void emit_c_string(FILE *out, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    fputc('"', out);
    while (*p) {
        unsigned char c = *p++;

        switch (c) {
        case '\\':
            fputs("\\\\", out);
            break;
        case '"':
            fputs("\\\"", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (c < 32 || c == 127) {
                fprintf(out, "\\%03o", c);
            } else {
                fputc(c, out);
            }
            break;
        }
    }
    fputc('"', out);
}

static void emit_line_label(FILE *out, int line)
{
    char label[128];

    sangen_line_label(line, label, sizeof(label));
    emit_c_string(out, label);
}

static int func_index(const EmitCtx *ctx, const char *name)
{
    int i;
    int index = 0;

    if (!ctx->program || ctx->program->kind != N_BLOCK) {
        return -1;
    }

    for (i = 0; i < ctx->program->nstmt; i++) {
        const Node *stmt = ctx->program->stmts[i];
        if (stmt->kind != N_FUNC) {
            continue;
        }
        if (stmt->name && strcmp(stmt->name, name) == 0) {
            return index;
        }
        index++;
    }

    return -1;
}

static void emit_expr(const Node *node, FILE *out, const EmitCtx *ctx);

static void emit_var_expr(const Node *node, FILE *out)
{
    fprintf(out, "require_var(d[%d], v[%d], ", node->var, node->var);
    emit_c_string(out, var_name(node->var));
    fputs(", ", out);
    emit_line_label(out, node->line);
    fputc(')', out);
}

static void emit_call_expr(const Node *node, FILE *out, const EmitCtx *ctx)
{
    int index = func_index(ctx, node->name ? node->name : "");

    if (index < 0) {
        fputs("missing_func(", out);
        emit_c_string(out, node->name ? node->name : "");
        fputs(", ", out);
        emit_line_label(out, node->line);
        fputc(')', out);
        return;
    }

    fprintf(out, "fn%d(v, d, ", index);
    emit_line_label(out, node->line);
    fputc(')', out);
}

static void emit_binexpr(const Node *node, FILE *out, const EmitCtx *ctx)
{
    if (node->op == T_SUM || node->op == T_DIFF || node->op == T_PROD ||
        node->op == T_QUOT || node->op == T_REM) {
        switch ((TokType)node->op) {
        case T_SUM:
            fputs("checked_add(", out);
            break;
        case T_DIFF:
            fputs("checked_sub(", out);
            break;
        case T_PROD:
            fputs("checked_mul(", out);
            break;
        case T_QUOT:
            fputs("checked_div(", out);
            break;
        case T_REM:
            fputs("checked_mod(", out);
            break;
        default:
            break;
        }
        emit_expr(node->lhs, out, ctx);
        fputs(", ", out);
        emit_expr(node->rhs, out, ctx);
        fputs(", ", out);
        emit_line_label(out, node->line);
        fputc(')', out);
        return;
    }

    fputc('(', out);
    emit_expr(node->lhs, out, ctx);
    switch ((TokType)node->op) {
    case T_SUM:
        fputs(" + ", out);
        break;
    case T_DIFF:
        fputs(" - ", out);
        break;
    case T_PROD:
        fputs(" * ", out);
        break;
    default:
        fputs(" ? ", out);
        break;
    }
    emit_expr(node->rhs, out, ctx);
    fputc(')', out);
}

static void emit_compare(const Node *node, FILE *out, const EmitCtx *ctx)
{
    fputc('(', out);
    emit_expr(node->lhs, out, ctx);
    switch ((TokType)node->op) {
    case T_GT:
        fputs(" > ", out);
        break;
    case T_LT:
        fputs(" < ", out);
        break;
    case T_EQ:
        fputs(" == ", out);
        break;
    default:
        fputs(" ? ", out);
        break;
    }
    emit_expr(node->rhs, out, ctx);
    fputc(')', out);
}

static void emit_divis(const Node *node, FILE *out, const EmitCtx *ctx)
{
    fprintf(out, "divisible(require_var(d[%d], v[%d], ", node->dividend, node->dividend);
    emit_c_string(out, var_name(node->dividend));
    fputs(", ", out);
    emit_line_label(out, node->line);
    fputs("), ", out);
    emit_expr(node->divisor, out, ctx);
    fprintf(out, ", %d, ", node->want_no_rem);
    emit_line_label(out, node->line);
    fputc(')', out);
}

static void emit_expr(const Node *node, FILE *out, const EmitCtx *ctx)
{
    switch (node->kind) {
    case N_NUM:
        fprintf(out, "%ldL", node->num);
        break;
    case N_VAR:
        emit_var_expr(node, out);
        break;
    case N_CALL:
        emit_call_expr(node, out, ctx);
        break;
    case N_BINEXPR:
        emit_binexpr(node, out, ctx);
        break;
    case N_DIVIS:
        emit_divis(node, out, ctx);
        break;
    case N_COMPARE:
        emit_compare(node, out, ctx);
        break;
    default:
        fputs("0", out);
        break;
    }
}

static void emit_stmt(const Node *node, FILE *out, int depth, int *temp_id,
                      const EmitCtx *ctx);

static void emit_block(const Node *node, FILE *out, int depth, int *temp_id,
                       const EmitCtx *ctx)
{
    int i;

    for (i = 0; i < node->nstmt; i++) {
        emit_stmt(node->stmts[i], out, depth, temp_id, ctx);
    }
}

static void emit_for(const Node *node, FILE *out, int depth, int *temp_id,
                     const EmitCtx *ctx)
{
    int id = (*temp_id)++;

    indent(out, depth);
    fputs("{\n", out);
    indent(out, depth + 1);
    fprintf(out, "long __from%d = ", id);
    emit_expr(node->from, out, ctx);
    fputs(";\n", out);
    indent(out, depth + 1);
    fprintf(out, "long __to%d = ", id);
    emit_expr(node->to, out, ctx);
    fputs(";\n", out);
    indent(out, depth + 1);
    fprintf(out, "if (__from%d > __to%d) runtime_error_at(", id, id);
    emit_line_label(out, node->line);
    fputs(", \"始大於終不可歷\");\n", out);
    indent(out, depth + 1);
    fprintf(out, "for (long __i%d = __from%d;; __i%d++) {\n", id, id, id);
    indent(out, depth + 2);
    fprintf(out, "v[%d] = __i%d;\n", node->var, id);
    indent(out, depth + 2);
    fprintf(out, "d[%d] = 1;\n", node->var);
    emit_block(node->body, out, depth + 2, temp_id, ctx);
    indent(out, depth + 2);
    fprintf(out, "if (__i%d == __to%d) break;\n", id, id);
    indent(out, depth + 1);
    fputs("}\n", out);
    indent(out, depth);
    fputs("}\n", out);
}

static void emit_while(const Node *node, FILE *out, int depth, int *temp_id,
                       const EmitCtx *ctx)
{
    (void)temp_id;
    indent(out, depth);
    fputs("while (", out);
    emit_expr(node->expr, out, ctx);
    fputs(") {\n", out);
    emit_block(node->body, out, depth + 1, temp_id, ctx);
    indent(out, depth);
    fputs("}\n", out);
}

static void emit_if(const Node *node, FILE *out, int depth, int *temp_id,
                    const EmitCtx *ctx)
{
    int i;

    for (i = 0; i < node->nbranch; i++) {
        indent(out, depth);
        fputs(i == 0 ? "if (" : "} else if (", out);
        emit_expr(node->conds[i], out, ctx);
        fputs(") {\n", out);
        emit_block(node->thens[i], out, depth + 1, temp_id, ctx);
    }

    if (node->els) {
        indent(out, depth);
        fputs("} else {\n", out);
        emit_block(node->els, out, depth + 1, temp_id, ctx);
    }

    indent(out, depth);
    fputs("}\n", out);
}

static void emit_stmt(const Node *node, FILE *out, int depth, int *temp_id,
                      const EmitCtx *ctx)
{
    switch (node->kind) {
    case N_BLOCK:
        emit_block(node, out, depth, temp_id, ctx);
        break;
    case N_ASSIGN:
        indent(out, depth);
        fprintf(out, "v[%d] = ", node->var);
        emit_expr(node->expr, out, ctx);
        fprintf(out, "; d[%d] = 1;\n", node->var);
        break;
    case N_SAY:
        indent(out, depth);
        fputs("puts(", out);
        emit_c_string(out, node->str ? node->str : "");
        fputs(");\n", out);
        break;
    case N_WRITE:
        indent(out, depth);
        fputs("printf(\"%ld\\n\", ", out);
        emit_expr(node->expr, out, ctx);
        fputs(");\n", out);
        break;
    case N_FOR:
        emit_for(node, out, depth, temp_id, ctx);
        break;
    case N_WHILE:
        emit_while(node, out, depth, temp_id, ctx);
        break;
    case N_IF:
        emit_if(node, out, depth, temp_id, ctx);
        break;
    case N_CALL:
        indent(out, depth);
        emit_call_expr(node, out, ctx);
        fputs(";\n", out);
        break;
    case N_FUNC:
        break;
    default:
        break;
    }
}

static void emit_helpers(FILE *out)
{
    fputs("#include <stdio.h>\n", out);
    fputs("#include <limits.h>\n", out);
    fputs("#include <stdlib.h>\n\n", out);
    fputs("void runtime_error_at(const char *line, const char *message) {\n", out);
    fputs("    fprintf(stderr, \"%s%s\\n\", line, message);\n", out);
    fputs("    exit(EXIT_FAILURE);\n", out);
    fputs("}\n\n", out);
    fputs("long require_var(int defined, long value, const char *name, const char *line) {\n", out);
    fputs("    char msg[128];\n", out);
    fputs("    if (!defined) {\n", out);
    fputs("        snprintf(msg, sizeof(msg), \"%s未有定值\", name);\n", out);
    fputs("        runtime_error_at(line, msg);\n", out);
    fputs("    }\n", out);
    fputs("    return value;\n", out);
    fputs("}\n\n", out);
    fputs("long missing_func(const char *name, const char *line) {\n", out);
    fputs("    char msg[256];\n", out);
    fputs("    snprintf(msg, sizeof(msg), \"術%s未立\", name);\n", out);
    fputs("    runtime_error_at(line, msg);\n", out);
    fputs("    return 0;\n", out);
    fputs("}\n\n", out);
    fputs("long checked_add(long lhs, long rhs, const char *line) {\n", out);
    fputs("    if ((rhs > 0 && lhs > LONG_MAX - rhs) || (rhs < 0 && lhs < LONG_MIN - rhs)) runtime_error_at(line, \"數溢\");\n", out);
    fputs("    return lhs + rhs;\n", out);
    fputs("}\n\n", out);
    fputs("long checked_sub(long lhs, long rhs, const char *line) {\n", out);
    fputs("    if ((rhs < 0 && lhs > LONG_MAX + rhs) || (rhs > 0 && lhs < LONG_MIN + rhs)) runtime_error_at(line, \"數溢\");\n", out);
    fputs("    return lhs - rhs;\n", out);
    fputs("}\n\n", out);
    fputs("long checked_mul(long lhs, long rhs, const char *line) {\n", out);
    fputs("    if (lhs > 0) {\n", out);
    fputs("        if ((rhs > 0 && lhs > LONG_MAX / rhs) || (rhs < 0 && rhs < LONG_MIN / lhs)) runtime_error_at(line, \"數溢\");\n", out);
    fputs("    } else if (lhs < 0) {\n", out);
    fputs("        if ((rhs > 0 && lhs < LONG_MIN / rhs) || (rhs < 0 && rhs < LONG_MAX / lhs)) runtime_error_at(line, \"數溢\");\n", out);
    fputs("    }\n", out);
    fputs("    return lhs * rhs;\n", out);
    fputs("}\n\n", out);
    fputs("long checked_div(long lhs, long rhs, const char *line) {\n", out);
    fputs("    if (rhs == 0) runtime_error_at(line, \"零不可為法\");\n", out);
    fputs("    if (lhs == LONG_MIN && rhs == -1) runtime_error_at(line, \"數溢\");\n", out);
    fputs("    return lhs / rhs;\n", out);
    fputs("}\n\n", out);
    fputs("long checked_mod(long lhs, long rhs, const char *line) {\n", out);
    fputs("    if (rhs == 0) runtime_error_at(line, \"零不可為法\");\n", out);
    fputs("    if (lhs == LONG_MIN && rhs == -1) runtime_error_at(line, \"數溢\");\n", out);
    fputs("    return lhs % rhs;\n", out);
    fputs("}\n\n", out);
    fputs("int divisible(long dividend, long divisor, int want_no_rem, const char *line) {\n", out);
    fputs("    int no_rem;\n", out);
    fputs("    if (divisor == 0) runtime_error_at(line, \"零不可為法\");\n", out);
    fputs("    no_rem = (dividend % divisor) == 0;\n", out);
    fputs("    return no_rem == want_no_rem;\n", out);
    fputs("}\n\n", out);
}

static void emit_prototypes(FILE *out, const EmitCtx *ctx)
{
    int i;
    int index = 0;

    if (!ctx->program || ctx->program->kind != N_BLOCK) {
        return;
    }

    for (i = 0; i < ctx->program->nstmt; i++) {
        if (ctx->program->stmts[i]->kind == N_FUNC) {
            fprintf(out, "long fn%d(long caller_v[10], int caller_d[10], const char *call_line);\n", index++);
        }
    }

    if (index > 0) {
        fputc('\n', out);
    }
}

static void emit_function(FILE *out, const Node *func, int index, const EmitCtx *ctx)
{
    int i;
    int temp_id = 0;

    fprintf(out, "long fn%d(long caller_v[10], int caller_d[10], const char *call_line) {\n", index);
    fputs("    long v[10] = {0};\n", out);
    fputs("    int d[10] = {0};\n", out);
    fputs("    (void)caller_v; (void)caller_d; (void)v; (void)d;\n", out);

    for (i = 0; i < func->nparam; i++) {
        int var = func->params[i];
        indent(out, 1);
        fprintf(out, "v[%d] = require_var(caller_d[%d], caller_v[%d], ", var, var, var);
        emit_c_string(out, var_name(var));
        fprintf(out, ", call_line); d[%d] = 1;\n", var);
    }

    emit_block(func->body, out, 1, &temp_id, ctx);
    indent(out, 1);
    fputs("return ", out);
    emit_expr(func->expr, out, ctx);
    fputs(";\n", out);
    fputs("}\n\n", out);
}

static void emit_functions(FILE *out, const EmitCtx *ctx)
{
    int i;
    int index = 0;

    if (!ctx->program || ctx->program->kind != N_BLOCK) {
        return;
    }

    for (i = 0; i < ctx->program->nstmt; i++) {
        const Node *stmt = ctx->program->stmts[i];
        if (stmt->kind == N_FUNC) {
            emit_function(out, stmt, index++, ctx);
        }
    }
}

void emit_c_program(const Node *program, FILE *out)
{
    EmitCtx ctx;
    int temp_id = 0;

    ctx.program = program;

    emit_helpers(out);
    emit_prototypes(out, &ctx);
    emit_functions(out, &ctx);

    fputs("int main(void) {\n", out);
    fputs("    long v[10] = {0};\n", out);
    fputs("    int d[10] = {0};\n", out);
    fputs("    (void)v; (void)d;\n", out);
    emit_stmt(program, out, 1, &temp_id, &ctx);
    fputs("    return 0;\n", out);
    fputs("}\n", out);
}
