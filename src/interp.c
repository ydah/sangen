#include "interp.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"
#include "support.h"

void env_init(Env *env)
{
    memset(env->vars, 0, sizeof(env->vars));
    memset(env->defined, 0, sizeof(env->defined));
    env->funcs = NULL;
    env->nfunc = 0;
    env->error = NULL;
    env->failed = 0;
}

void env_free(Env *env)
{
    free(env->funcs);
    env->funcs = NULL;
    env->nfunc = 0;
    free(env->error);
    env->error = NULL;
    env->failed = 0;
}

static void runtime_error(Env *env, const char *message)
{
    if (env->failed) {
        return;
    }
    env->error = sangen_xstrdup(message);
    env->failed = 1;
}

static void runtime_error_at(Env *env, int line, const char *message)
{
    char label[128];
    char msg[512];

    if (env->failed) {
        return;
    }
    sangen_line_label(line, label, sizeof(label));
    snprintf(msg, sizeof(msg), "%s%s", label, message);
    runtime_error(env, msg);
}

static long require_var(Env *env, int var, int line)
{
    char msg[128];

    if (var < 0 || var >= 10) {
        runtime_error_at(env, line, "天干不可識");
        return 0;
    }

    if (!env->defined[var]) {
        snprintf(msg, sizeof(msg), "%s未有定值", var_name(var));
        runtime_error_at(env, line, msg);
        return 0;
    }

    return env->vars[var];
}

static Node *find_func(Env *env, const char *name)
{
    int i;

    for (i = env->nfunc - 1; i >= 0; i--) {
        if (env->funcs[i]->name && strcmp(env->funcs[i]->name, name) == 0) {
            return env->funcs[i];
        }
    }

    return NULL;
}

static void register_func(Env *env, Node *func)
{
    if (find_func(env, func->name)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "術%s既立", func->name ? func->name : "");
        runtime_error_at(env, func->line, msg);
        return;
    }

    env->funcs = sangen_xrealloc(env->funcs,
                                 sizeof(Node *) * (size_t)(env->nfunc + 1));
    env->funcs[env->nfunc++] = func;
}

static long call_func(Node *call, Env *env)
{
    Node *func = find_func(env, call->name ? call->name : "");
    Env local;
    int i;

    if (!func) {
        char msg[256];
        snprintf(msg, sizeof(msg), "術%s未立", call->name ? call->name : "");
        runtime_error_at(env, call->line, msg);
        return 0;
    }

    env_init(&local);
    local.funcs = env->funcs;
    local.nfunc = env->nfunc;

    for (i = 0; i < func->nparam; i++) {
        int var = func->params[i];
        local.vars[var] = require_var(env, var, call->line);
        if (env->failed) {
            return 0;
        }
        local.defined[var] = 1;
    }

    exec(func->body, &local);
    if (!local.failed) {
        long value = eval(func->expr, &local);
        if (!local.failed) {
            return value;
        }
    }

    if (local.failed && !env->failed) {
        env->error = local.error;
        env->failed = 1;
        local.error = NULL;
    }
    return 0;
}

static long checked_add(Env *env, long lhs, long rhs, int line)
{
    if ((rhs > 0 && lhs > LONG_MAX - rhs) ||
        (rhs < 0 && lhs < LONG_MIN - rhs)) {
        runtime_error_at(env, line, "數溢");
        return 0;
    }

    return lhs + rhs;
}

static long checked_sub(Env *env, long lhs, long rhs, int line)
{
    if ((rhs < 0 && lhs > LONG_MAX + rhs) ||
        (rhs > 0 && lhs < LONG_MIN + rhs)) {
        runtime_error_at(env, line, "數溢");
        return 0;
    }

    return lhs - rhs;
}

static long checked_mul(Env *env, long lhs, long rhs, int line)
{
    if (lhs > 0) {
        if ((rhs > 0 && lhs > LONG_MAX / rhs) ||
            (rhs < 0 && rhs < LONG_MIN / lhs)) {
            runtime_error_at(env, line, "數溢");
            return 0;
        }
    } else if (lhs < 0) {
        if ((rhs > 0 && lhs < LONG_MIN / rhs) ||
            (rhs < 0 && rhs < LONG_MAX / lhs)) {
            runtime_error_at(env, line, "數溢");
            return 0;
        }
    }

    return lhs * rhs;
}

static long checked_div(Env *env, long lhs, long rhs, int line)
{
    if (rhs == 0) {
        runtime_error_at(env, line, "零不可為法");
        return 0;
    }
    if (lhs == LONG_MIN && rhs == -1) {
        runtime_error_at(env, line, "數溢");
        return 0;
    }

    return lhs / rhs;
}

static long checked_mod(Env *env, long lhs, long rhs, int line)
{
    if (rhs == 0) {
        runtime_error_at(env, line, "零不可為法");
        return 0;
    }
    if (lhs == LONG_MIN && rhs == -1) {
        runtime_error_at(env, line, "數溢");
        return 0;
    }

    return lhs % rhs;
}

static long eval_binexpr(Node *e, Env *env)
{
    long lhs = eval(e->lhs, env);
    long rhs = eval(e->rhs, env);

    if (env->failed) {
        return 0;
    }

    switch ((TokType)e->op) {
    case T_SUM:
        return checked_add(env, lhs, rhs, e->line);
    case T_DIFF:
        return checked_sub(env, lhs, rhs, e->line);
    case T_PROD:
        return checked_mul(env, lhs, rhs, e->line);
    case T_QUOT:
        return checked_div(env, lhs, rhs, e->line);
    case T_REM:
        return checked_mod(env, lhs, rhs, e->line);
    default:
        runtime_error_at(env, e->line, "算術難識");
    }

    return 0;
}

static long eval_divis(Node *e, Env *env)
{
    long divisor = eval(e->divisor, env);
    long dividend = require_var(env, e->dividend, e->line);
    int no_rem;

    if (env->failed) {
        return 0;
    }

    if (divisor == 0) {
        runtime_error_at(env, e->line, "零不可為法");
        return 0;
    }

    no_rem = (dividend % divisor) == 0;
    return no_rem == e->want_no_rem ? 1 : 0;
}

static long eval_compare(Node *e, Env *env)
{
    long lhs = eval(e->lhs, env);
    long rhs = eval(e->rhs, env);

    if (env->failed) {
        return 0;
    }

    switch ((TokType)e->op) {
    case T_GT:
        return lhs > rhs;
    case T_LT:
        return lhs < rhs;
    case T_EQ:
        return lhs == rhs;
    default:
        runtime_error_at(env, e->line, "比較難識");
    }

    return 0;
}

long eval(Node *e, Env *env)
{
    if (!e) {
        runtime_error(env, "數難識");
        return 0;
    }

    if (env->failed) {
        return 0;
    }

    switch (e->kind) {
    case N_NUM:
        return e->num;
    case N_VAR:
        return require_var(env, e->var, e->line);
    case N_BINEXPR:
        return eval_binexpr(e, env);
    case N_DIVIS:
        return eval_divis(e, env);
    case N_COMPARE:
        return eval_compare(e, env);
    case N_CALL:
        return call_func(e, env);
    default:
        runtime_error_at(env, e->line, "數難識");
    }

    return 0;
}

static void exec_for(Node *s, Env *env)
{
    long from = eval(s->from, env);
    long to = eval(s->to, env);
    long i;

    if (env->failed) {
        return;
    }

    if (from > to) {
        runtime_error_at(env, s->line, "始大於終不可歷");
        return;
    }

    for (i = from;; i++) {
        env->vars[s->var] = i;
        env->defined[s->var] = 1;
        exec(s->body, env);
        if (env->failed) {
            return;
        }
        if (i == to) {
            break;
        }
    }
}

static void exec_if(Node *s, Env *env)
{
    int i;

    for (i = 0; i < s->nbranch; i++) {
        if (eval(s->conds[i], env) != 0) {
            if (env->failed) {
                return;
            }
            exec(s->thens[i], env);
            return;
        }
        if (env->failed) {
            return;
        }
    }

    if (s->els) {
        exec(s->els, env);
    }
}

static void exec_while(Node *s, Env *env)
{
    while (!env->failed && eval(s->expr, env) != 0) {
        exec(s->body, env);
    }
}

void exec(Node *s, Env *env)
{
    int i;

    if (!s || env->failed) {
        return;
    }

    switch (s->kind) {
    case N_BLOCK:
        for (i = 0; i < s->nstmt; i++) {
            exec(s->stmts[i], env);
            if (env->failed) {
                return;
            }
        }
        break;
    case N_ASSIGN:
        env->vars[s->var] = eval(s->expr, env);
        if (!env->failed) {
            env->defined[s->var] = 1;
        }
        break;
    case N_SAY:
        fputs(s->str ? s->str : "", stdout);
        fputc('\n', stdout);
        break;
    case N_WRITE:
        {
            long value = eval(s->expr, env);
            if (!env->failed) {
                printf("%ld\n", value);
            }
        }
        break;
    case N_FOR:
        exec_for(s, env);
        break;
    case N_WHILE:
        exec_while(s, env);
        break;
    case N_IF:
        exec_if(s, env);
        break;
    case N_FUNC:
        break;
    case N_CALL:
        (void)call_func(s, env);
        break;
    default:
        runtime_error_at(env, s->line, "句難識");
    }
}

int interp_program(Node *program, Env *env)
{
    int i;

    if (!program || program->kind != N_BLOCK) {
        exec(program, env);
        return !env->failed;
    }

    for (i = 0; i < program->nstmt; i++) {
        if (program->stmts[i]->kind == N_FUNC) {
            register_func(env, program->stmts[i]);
            if (env->failed) {
                return 0;
            }
        }
    }

    for (i = 0; i < program->nstmt; i++) {
        if (program->stmts[i]->kind != N_FUNC) {
            exec(program->stmts[i], env);
            if (env->failed) {
                return 0;
            }
        }
    }

    return 1;
}
