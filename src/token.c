#include "token.h"

const char *tok_type_name(TokType type)
{
    switch (type) {
    case T_EOF: return "文終";
    case T_NUMBER: return "數";
    case T_VAR: return "天干";
    case T_IDENT: return "術之名";
    case T_STRING: return "引辭";
    case T_FAN: return "凡";
    case T_FROM: return "自";
    case T_TO: return "至";
    case T_LOOP_END: return "焉";
    case T_WHILE: return "當";
    case T_IF: return "若";
    case T_THEN: return "則";
    case T_ELSE: return "不然";
    case T_IF_END: return "已矣";
    case T_MAKE: return "令";
    case T_BE: return "爲";
    case T_SAY: return "曰";
    case T_WRITE: return "書";
    case T_BY: return "以";
    case T_AND: return "而";
    case T_DIVIDE: return "除";
    case T_NO_REM: return "無餘";
    case T_HAS_REM: return "有餘";
    case T_GT: return "過";
    case T_LT: return "不及";
    case T_EQ: return "等";
    case T_WITH: return "與";
    case T_GEN: return "之";
    case T_SUM: return "和";
    case T_DIFF: return "差";
    case T_PROD: return "積";
    case T_QUOT: return "商";
    case T_REM: return "餘";
    case T_FUNC_START: return "夫";
    case T_PROC: return "術";
    case T_SUBJECT: return "者";
    case T_ASSERT: return "也";
    case T_ACCEPT: return "受";
    case T_RETURN: return "答";
    case T_USE: return "用";
    case T_FUNC_END: return "術畢";
    }

    return "?";
}
