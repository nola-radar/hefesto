/*
 *                              Copyright (C) 2013 by Rafael Santiago
 *
 * This is free software. You can redistribute it and/or modify under
 * the terms of the GNU General Public License version 2.
 *
 */
#include <string.h>
#include "expr_handler.h"
#include "structs_io.h"
#include "exprchk.h"
#include "synchk.h"
#include "parser.h"
#include "mem.h"

#define hefesto_set_expr_op(o,p) { o, p }

#define is_hefesto_op(o) ( o == '+' || o == '-' || o == '*' || o == '/' ||\
                           o == '=' || o == '>' || o == '<' || o == '&' ||\
                           o == '|' || o == '^' || o == '%' || o == '!')

static struct hefesto_expr_ops_ctx HEFESTO_EXPR_OPS[HEFESTO_EXPR_OPS_NR] = {
  hefesto_set_expr_op("+\0", 1),
  hefesto_set_expr_op("-\0", 1),
  hefesto_set_expr_op("*\0", 2),
  hefesto_set_expr_op("/\0", 2),
  hefesto_set_expr_op("==\0", 1),
  hefesto_set_expr_op(">>\0", 1),
  hefesto_set_expr_op("<<\0", 1),
  hefesto_set_expr_op("&&\0", 0),
  hefesto_set_expr_op("||\0", 0),
  hefesto_set_expr_op("&\0", 0),
  hefesto_set_expr_op("|\0", 0),
  hefesto_set_expr_op("^\0", 0),
  hefesto_set_expr_op("=\0", 0),
  hefesto_set_expr_op("%\0", 1),
  hefesto_set_expr_op("<=\0", 1),
  hefesto_set_expr_op(">=\0", 1),
  hefesto_set_expr_op("<\0", 1),
  hefesto_set_expr_op(">\0", 1),
  hefesto_set_expr_op("!=\0", 1)
};

#undef hefesto_set_expr_op

static char *infix2postfix_function_args(const char *expr_args,
                                         const size_t esize);

static hefesto_int_t is_postfixed(const char *expr);

static char *infix2postfix_function_args(const char *expr_args,
                                         const size_t esize) {

    char *e;
    char *iexpr, *pexpr;
    char *result = (char *) hefesto_mloc(HEFESTO_MAX_BUFFER_SIZE);
    char *r;
    size_t offset = 1;

    iexpr = get_arg_from_call(expr_args, &offset);

    r = result;
    *r = '(';
    r++;

    while (*iexpr) {
        pexpr = infix2postfix(iexpr, strlen(iexpr), 0);

        for (e = pexpr; *e != 0; e++, r++) *r = *e;

        free(iexpr);

        free(pexpr);

        iexpr = get_arg_from_call(expr_args, &offset);

        if (*iexpr) {
            *r = ',';
            r++;
        }
    }

    free(iexpr);

    *r = 0;

    HEFESTO_DEBUG_INFO(0, "expr_handler/function args: %s\n", result);

    return result;

}

char *infix2postfix(const char *expr, const size_t esize, const hefesto_int_t main_call) {

    const char *efinis = expr + esize, *e;
    char *t, *term, *tt;
    hefesto_common_stack_ctx *sp = NULL, *dp = NULL;
    hefesto_common_list_ctx *lp = NULL, *l;
    hefesto_int_t p, tp;
    size_t sz;
    hefesto_int_t bracket = 0;
    char *indexing;
    char *iexpr;
    char *args;
    size_t offset, count;
    hefesto_int_t state = 0;

    term = (char *) hefesto_mloc(HEFESTO_MAX_BUFFER_SIZE);
    memset(term, 0, HEFESTO_MAX_BUFFER_SIZE);

    t = term;
    *t = 0;

    for (e = expr; e != efinis; e++) {

        if (*e == 0) break;

        if (*e == '\n' || *e == '\r' || *e == '\t' || *e == ' ') continue;

        if (*e != 0 && *e != ' ' && *e != '\n' && *e != '\r' && *e != '\t' &&
            *e != '(' && *e != ')' && !is_op(*e) && *e != '[' &&
            !is_hefesto_string_tok(*e)) {
            *t = *e;
            t++;
            *t = 0;
            continue;
        } else if (is_hefesto_string_tok(*e)) {
            offset = 0;
            iexpr = get_next_string(e, &offset);
            e += offset;
            for (indexing = iexpr; *indexing != 0; indexing++, t++)
            *t = *indexing;
            free(iexpr);
            *t = 0;
        } else if (*e == '(' && !is_hefesto_numeric_constant(term) &&
               !is_hefesto_string(term) && *term && *term != '(' &&
               !is_op(*term)) {
            bracket++;
            offset = 0;
            args = get_next_call_args(e, &offset);

            if (*args) {
                iexpr = infix2postfix_function_args(args, strlen(args));
                tt = t;
                for (indexing = iexpr; *indexing != 0; indexing++, t++)
                    *t = *indexing;
                *t = 0;
                free(iexpr);
                count = 0;
                for (t = tt; *t != 0; t++) {
                    if (*t == '(') {
                        count++;
                    } else if (*t == ')') {
                        count--;
                    } else if (*t == '"') {
                        t++;
                        while (*t != '"' && *t != 0) {
                            t++;
                            if (*t == '\\') t += 2;
                        }
                    }
                }
                tt = ((char *)e + offset);
                while (is_hefesto_blank(*tt)) tt++;
                // can't add one more ')' because this is a function that is an
                // argument of another function
                if (*tt == ',') count--;
                while (count > 0) {
                    *t = ')';
                    t++;
                    count--;
                }
                *t = 0;
            }
            e += offset;
            free(args);
            if (*e == ')') e++;
        } else if ((*e == '-' || *e == '+') && t == term && isdigit(*(e+1)) &&
                   state == 0) {
            *t = *e;
            t++;
            continue;
        }

        if ((is_op(*e) || *e == '(' || *e == ')') && t == term) {
            *t = *e;
            t++;
            *t = 0;
            if (is_op(*(e+1)) && *e != '(' && *e != ')') { // composed operands
                *t = *(e+1);
                e++;
                t++;
                *t = 0;
            }
            state = 0;
        } else if ((e + 1) != efinis) e--;

        if (strcmp(term,"(") == 0) {
            sp = hefesto_common_stack_ctx_push(sp, (void *) term, t - term,
                                               HEFESTO_VAR_TYPE_UNTYPED);
        } else if(strcmp(term, ")") == 0) {
            t = (char *)hefesto_common_stack_ctx_data_on_top(sp);
            if (t && strcmp(t, "(") != 0) {
                while (sp) {
                    lp = add_data_to_hefesto_common_list_ctx(lp,
                                    hefesto_common_stack_ctx_data_on_top(sp),
                                    hefesto_common_stack_ctx_dsize_on_top(sp));
                    dp = hefesto_common_stack_ctx_on_top(sp);
                    sp = hefesto_common_stack_ctx_pop(sp);
                    del_hefesto_common_stack_ctx(dp);
                }
            }
            dp = hefesto_common_stack_ctx_on_top(sp);
            sp = hefesto_common_stack_ctx_pop(sp);
            del_hefesto_common_stack_ctx(dp);
        } else if ((p = get_op_precedence(term)) != -1) {

            tp = get_op_precedence(hefesto_common_stack_ctx_data_on_top(sp)); 

            while (tp != -1 && tp >= p) {
                lp = add_data_to_hefesto_common_list_ctx(lp,
                                    hefesto_common_stack_ctx_data_on_top(sp),
                                    hefesto_common_stack_ctx_dsize_on_top(sp));
                dp = hefesto_common_stack_ctx_on_top(sp);
                sp = hefesto_common_stack_ctx_pop(sp);
                del_hefesto_common_stack_ctx(dp);
                tp = get_op_precedence(hefesto_common_stack_ctx_data_on_top(sp)); 
            }

            sp = hefesto_common_stack_ctx_push(sp, (void *) term, t - term,
                                                   HEFESTO_VAR_TYPE_UNTYPED);
        } else {
            lp = add_data_to_hefesto_common_list_ctx(lp, term, t - term);
            state = 1;
        }
        t = term;
        *t = 0;

    }

    if (*term) {
        HEFESTO_DEBUG_INFO(0, "expr_handler/pushing \"%s\"\n", term);
        sp = hefesto_common_stack_ctx_push(sp, (void *) term, t - term,
                                           HEFESTO_VAR_TYPE_UNTYPED);
    }

    while (!hefesto_common_stack_ctx_empty(sp)) {
        lp = add_data_to_hefesto_common_list_ctx(lp,
                        hefesto_common_stack_ctx_data_on_top(sp),
                        hefesto_common_stack_ctx_dsize_on_top(sp));
        dp = hefesto_common_stack_ctx_on_top(sp);
        sp = hefesto_common_stack_ctx_pop(sp);
        del_hefesto_common_stack_ctx(dp);
    }

    free(term);

    for (p = 0, sz = 0, l = lp; l; l = l->next, p++) {
        sz += l->dsize;
    }

    sz += (size_t) p;

    term = (char *) hefesto_mloc(sz);
    *term = 0;

    for (l = lp; l; l = l->next) {
        if (*(char *)l->data == '(') continue;
        strncat(term, (char *) l->data, sz - 1);
        if (l->next && *(char *)l->next->data != '[')
        strncat(term, " ", sz - 1);
    }

    del_hefesto_common_list_ctx(lp);
    return term;

}

hefesto_int_t get_op_precedence(const char *op) {

    hefesto_int_t o;

    if (op == NULL) return -1;

    for ( o = 0; o < HEFESTO_EXPR_OPS_NR; o++)
        if (strcmp(HEFESTO_EXPR_OPS[o].op, op) == 0)
            return HEFESTO_EXPR_OPS[o].precedence;

    return -1;

}

ssize_t get_op_index(const char *op) {

    ssize_t op_idx;

    for (op_idx = 0; op_idx != HEFESTO_EXPR_OPS_NR; op_idx++)
        if (strcmp(HEFESTO_EXPR_OPS[op_idx].op, op) == 0) return op_idx;

    return -1;

}

static hefesto_int_t is_postfixed(const char *expr) { // deprecated
    const char *ep;
    hefesto_int_t state = 0;
    char *buffer = (char *) hefesto_mloc(HEFESTO_MAX_BUFFER_SIZE);
    char *b_end = buffer + HEFESTO_MAX_BUFFER_SIZE;
    char *bp = buffer;
    hefesto_int_t retval = 0;
    for (ep = expr; *ep != 0 && !retval && state < 3; ep++) {
        if (!is_hefesto_blank(*ep) && !is_hefesto_op(*ep)) {
            if (bp < b_end) {
                *bp = *ep;
                bp++;
                if (is_hefesto_string_tok(*ep)) {
                    ep++;
                    while (!is_hefesto_string_tok(*ep) &&
                           *ep != 0 && bp < b_end) {
                        *bp = *ep;
                        if (*ep == '\\') {
                            bp++;
                            ep++;
                            *bp = *ep;
                        }
                        bp++;
                        ep++;
                    }
                    *bp = *ep;
                    bp++;
                }
            }
        } else {
            if (bp == buffer && is_hefesto_op(*ep)) {
                *bp = *ep;
                ep++;
                bp++;
            }
            if (bp == buffer) continue;
            *bp = 0;
            switch (state) {
                case 0:
                case 1:
                    if (is_hefesto_op(*buffer)) {
                        state = 3; // it is not the expected.
                    }
                    state++;
                    break;

                case 2:
                    if (!is_hefesto_op(*buffer)) {
                        state = 3; // it is not the expected.
                    }
                    state++;
                    if (state == 3) {
                        retval = 1;
                    }
                    break;
            }
            bp = buffer;
            if (is_hefesto_op(*ep)) ep--;
        }
    }
    free(buffer);
    return retval;
}
