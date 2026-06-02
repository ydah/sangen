#ifndef SANGEN_TOKEN_H
#define SANGEN_TOKEN_H

typedef enum {
    T_EOF,
    T_NUMBER,
    T_VAR,
    T_IDENT,
    T_HAN,
    T_STRING,
    T_FAN,
    T_FROM,
    T_TO,
    T_LOOP_END,
    T_WHILE,
    T_IF,
    T_THEN,
    T_ELSE,
    T_IF_END,
    T_MAKE,
    T_BE,
    T_SAY,
    T_WRITE,
    T_BY,
    T_AND,
    T_DIVIDE,
    T_NO,
    T_HAVE,
    T_NO_REM,
    T_HAS_REM,
    T_GT,
    T_LT,
    T_EQ,
    T_AT,
    T_WITH,
    T_GEN,
    T_SUM,
    T_DIFF,
    T_PROD,
    T_QUOT,
    T_REM,
    T_FUNC_START,
    T_PROC,
    T_SUBJECT,
    T_ASSERT,
    T_ACCEPT,
    T_RETURN,
    T_DO,
    T_USE,
    T_FUNC_END
} TokType;

typedef struct {
    TokType type;
    long lval;
    char *sval;
    int line;
    int col;
    unsigned long start;
    unsigned long end;
} Token;

const char *tok_type_name(TokType type);

#endif
