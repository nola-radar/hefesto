#ifndef _HEFESTO_HVM_MOD_H
#define _HEFESTO_HVM_MOD_H 1

#include "types.h"

void *hvm_mod_call(const char *call, hefesto_var_list_ctx **lo_vars,
                   hefesto_var_list_ctx **gl_vars,
                   hefesto_func_list_ctx *functions, hefesto_type_t **otype);

#endif