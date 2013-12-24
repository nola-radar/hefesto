#include "hvm_mod.h"
#include "hlsc_msg.h"
#include "synchk.h"
#include "hvm_alu.h"
#include "structs_io.h"
#include "parser.h"
#include <string.h>

#if HEFESTO_TGT_OS == HEFESTO_LINUX || HEFESTO_TGT_OS == HEFESTO_FREEBSD

#include <dlfcn.h>

typedef void * hefesto_mod_handle;

#elif HEFESTO_TGT_OS == HEFESTO_WINDOWS

#include <windows.h>

typedef HMODULE hefesto_mod_handle;

#endif

struct hefesto_modio_args {
    void *data;
    int dtype;
    size_t dsize;
    int byref_on_return;
    struct hefesto_modio_args *next;
};

struct hefesto_modio {
    struct hefesto_modio_args *args;
    int rtype;
    size_t rsize;
    void *ret;
};

typedef void (*hefesto_modfunc)(struct hefesto_modio **);

struct hefesto_ldmod_table {
    char module_path[HEFESTO_MAX_BUFFER_SIZE];
    hefesto_mod_handle handle;
    int ref_count;
    char last_called_sym[HEFESTO_MAX_BUFFER_SIZE];
    hefesto_modfunc sym;
};

#define HEFESTO_LDMOD_TABLE_SIZE 10

#define HEFESTO_LDMOD_MAX_REF_COUNT  20

static struct hefesto_ldmod_table HEFESTO_LDMOD_TABLE[HEFESTO_LDMOD_TABLE_SIZE] = {
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL},
    {"(null)", NULL, 0, "(null)", NULL}
};

#define new_hefesto_modio_args(a) ((a) = (struct hefesto_modio_args *)\
                               hefesto_mloc(sizeof(struct hefesto_modio_args)),\
                               (a)->data = NULL,\
                               (a)->dsize = 0,\
                               (a)->dtype = HEFESTO_VAR_TYPE_UNTYPED,\
                               (a)->byref_on_return = 0,\
                               (a)->next = NULL )

#define new_hefesto_modio(m) ( (m) = (struct hefesto_modio *)\
                               hefesto_mloc(sizeof(struct hefesto_modio)),\
                               (m)->args = NULL,\
                               (m)->ret = NULL,\
                               (m)->rsize = 0,\
                               (m)->rtype = HEFESTO_VAR_TYPE_UNTYPED )


static hefesto_mod_handle hvm_mod_load(const char *module_filepath);

static int hvm_mod_close(hefesto_mod_handle mod_handle);

static hefesto_modfunc hvm_mod_get_sym(hefesto_mod_handle mod_handle,
                                       const char *function);

static void del_hefesto_modio(struct hefesto_modio *modio);

static void del_hefesto_modio_args(struct hefesto_modio_args *args);

static void handle_byref(struct hefesto_modio *modio,
                         hefesto_var_list_ctx *args,
                         hefesto_var_list_ctx **lo_vars,
                         hefesto_var_list_ctx **gl_vars);

static void del_hefesto_modio_args(struct hefesto_modio_args *args) {
    struct hefesto_modio_args *t, *p;
    for (t = p = args; t; p = t) {
        t = p->next;
        if (p->data) free(p->data);
    }
}

static void del_hefesto_modio(struct hefesto_modio *modio) {
    if (modio == NULL) return;
    if (modio->args != NULL) {
        del_hefesto_modio_args(modio->args);
    }
    if (modio->ret != NULL) free(modio->ret);
    free(modio);
}

static hefesto_mod_handle hvm_mod_load(const char *module_filepath) {
    size_t l = 0;
#if HEFESTO_TGT_OS == HEFESTO_WINDOWS
    LPSTR err_msg = NULL;
#endif
    for (l = 0; l < HEFESTO_LDMOD_TABLE_SIZE; l++) {
        if (strcmp(module_filepath, HEFESTO_LDMOD_TABLE[l].module_path) == 0) {
            return HEFESTO_LDMOD_TABLE[l].handle;
        }
    }
    hefesto_mod_handle hp = NULL;
#if HEFESTO_TGT_OS == HEFESTO_LINUX || HEFESTO_TGT_OS == HEFESTO_FREEBSD
    hp = dlopen(module_filepath, RTLD_LAZY);
    if (hp == NULL) {
        hlsc_info(HLSCM_MTYPE_RUNTIME, HLSCM_RUNTIME_UNBALE_TO_LOAD_MODULE,
                  module_filepath, dlerror());
    }
#elif HEFESTO_TGT_OS == HEFESTO_WINDOWS
    hp = LoadLibrary(module_filepath);
    if (hp == NULL) {
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, GetLastError(),
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      err_msg, 0, NULL);
        hlsc_info(HLSCM_MTYPE_RUNTIME, HLSCM_RUNTIME_UNBALE_TO_LOAD_MODULE,
                  module_filepath, err_msg);
        LocalFree(err_msg);
    }
#endif
    if (hp != NULL) {
        for (l = 0; l < HEFESTO_LDMOD_TABLE_SIZE; l++) {
            if (HEFESTO_LDMOD_TABLE[l].handle == NULL) {
                memset(HEFESTO_LDMOD_TABLE[l].module_path, 0,
                       sizeof(HEFESTO_LDMOD_TABLE[l].module_path));
                strncpy(HEFESTO_LDMOD_TABLE[l].module_path, module_filepath,
                        sizeof(HEFESTO_LDMOD_TABLE[l].module_path)-1);
                HEFESTO_LDMOD_TABLE[l].handle = hp;
                HEFESTO_LDMOD_TABLE[l].ref_count = HEFESTO_LDMOD_MAX_REF_COUNT;
                break;
            }
        }
    }
    return hp;
}

static int hvm_mod_close(hefesto_mod_handle mod_handle) {
    size_t l;
    int retval = 0;
#if HEFESTO_TGT_OS == HEFESTO_LINUX || HEFESTO_TGT_OS == HEFESTO_FREEBSD
    if (mod_handle != NULL) {
        retval = dlclose(mod_handle);
    }
#elif HEFESTO_TGT_OS == HEFESTO_WINDOWS
    retval = (FreeLibrary(mod_handle) != 1);
#endif
    for (l = 0; l < HEFESTO_LDMOD_TABLE_SIZE; l++) {
        if (mod_handle == HEFESTO_LDMOD_TABLE[l].handle) {
            memset(HEFESTO_LDMOD_TABLE[l].module_path, 0,
                   sizeof(HEFESTO_LDMOD_TABLE[l].module_path));
            HEFESTO_LDMOD_TABLE[l].handle = NULL;
            HEFESTO_LDMOD_TABLE[l].ref_count = 0;
            memset(HEFESTO_LDMOD_TABLE[l].last_called_sym, 0,
                   sizeof(HEFESTO_LDMOD_TABLE[l].last_called_sym));
            HEFESTO_LDMOD_TABLE[l].sym = NULL;
            break;
        }
    }
    return retval;
}

void hvm_mod_ldmod_table_cleanup() {
    size_t l;
    for (l = 0; l < HEFESTO_LDMOD_TABLE_SIZE; l++) {
        if (HEFESTO_LDMOD_TABLE[l].handle != NULL) {
            hvm_mod_close(HEFESTO_LDMOD_TABLE[l].handle);
        }
    }
}

static hefesto_modfunc hvm_mod_get_sym(hefesto_mod_handle mod_handle,
                               const char *function) {
    hefesto_modfunc fn_p = NULL;
    size_t l;
    for (l = 0; l < HEFESTO_LDMOD_TABLE_SIZE; l++) {
        if (HEFESTO_LDMOD_TABLE[l].handle == mod_handle) {
            if (strcmp(HEFESTO_LDMOD_TABLE[l].last_called_sym, function) == 0) {
                fn_p = HEFESTO_LDMOD_TABLE[l].sym;
            }
            break;
        }
    }

    if (mod_handle != NULL && fn_p == NULL) {
#if HEFESTO_TGT_OS == HEFESTO_LINUX || HEFESTO_TGT_OS == HEFESTO_FREEBSD
        fn_p = dlsym(mod_handle, function);
#elif HEFESTO_TGT_OS == HEFESTO_WINDOWS
        fn_p = (hefesto_modfunc) GetProcAddress(mod_handle, function);
#endif
        if (l < HEFESTO_LDMOD_TABLE_SIZE) {
            memset(HEFESTO_LDMOD_TABLE[l].last_called_sym, 0,
                   sizeof(HEFESTO_LDMOD_TABLE[l].last_called_sym));
            strncpy(HEFESTO_LDMOD_TABLE[l].last_called_sym, function,
                    sizeof(HEFESTO_LDMOD_TABLE[l].last_called_sym)-1);
            HEFESTO_LDMOD_TABLE[l].sym = fn_p;
        }
    }
    if (fn_p == NULL) {
        hlsc_info(HLSCM_MTYPE_RUNTIME, HLSCM_RUNTIME_UNABLE_TO_FIND_SYMBOL, function);
    }

    for (l = 0; l < HEFESTO_LDMOD_TABLE_SIZE; l++) {
        if (mod_handle == HEFESTO_LDMOD_TABLE[l].handle) {
            HEFESTO_LDMOD_TABLE[l].ref_count--;
            if (HEFESTO_LDMOD_TABLE[l].ref_count == 0) {
                hvm_mod_close(HEFESTO_LDMOD_TABLE[l].handle);
            }
            break;
        }
    }
    return fn_p;
}

void *hvm_mod_call(const char *call, hefesto_var_list_ctx **lo_vars,
                   hefesto_var_list_ctx **gl_vars,
                   hefesto_func_list_ctx *functions, hefesto_type_t **otype) {
    hefesto_var_list_ctx *args = NULL, *ap;
    char *arg, *arg_pfxd;
    const char *s;
    size_t offset, osz;
    hefesto_type_t etype;
    void *res = NULL;
    hefesto_mod_handle module;
    hefesto_modfunc modfunc;
    struct hefesto_modio *modio;
    struct hefesto_modio_args *map, *mio_next;
    hefesto_var_list_ctx *vp;

    **otype = HEFESTO_VAR_TYPE_NONE;

    // parsing all the arguments from user call and expand the content
    // from these...

    s = get_arg_list_start_from_call(call);
    offset = s - call + 1;

    arg = get_arg_from_call(call, &offset);

    while (*arg) {
        arg_pfxd = infix2postfix(arg, strlen(arg), 1);
        etype = HEFESTO_VAR_TYPE_STRING;
        res = expr_eval(arg_pfxd, lo_vars, gl_vars, functions,
                        &etype, &osz);
        if (etype != HEFESTO_VAR_TYPE_STRING && etype != HEFESTO_VAR_TYPE_INT) {
            etype = HEFESTO_VAR_TYPE_INT;
        }
        args = add_var_to_hefesto_var_list_ctx(args, arg_pfxd, etype);
        ap = get_hefesto_var_list_ctx_tail(args);
        ap->contents = add_data_to_hefesto_common_list_ctx(ap->contents,
                                                           res, osz);
        free(res);
        free(arg_pfxd);
        free(arg);
        arg = get_arg_from_call(call, &offset);
    }

    free(arg);

    res = NULL;

    // the first and second arguments from list must be the
    // module filepath and symbol name.

    if (args != NULL) {
        ap = args;
        module = hvm_mod_load(ap->contents->data);
        ap = ap->next;
        if (module != NULL && ap != NULL &&
            args->contents != NULL) {
            modfunc = hvm_mod_get_sym(module, ap->contents->data);
            if (modfunc != NULL) {
                new_hefesto_modio(modio);
                map = NULL;
                mio_next = NULL;
                // loading the other variables into module i/o structure
                for (ap = ap->next; ap != NULL; ap = ap->next) {
                    new_hefesto_modio_args(map);
                    map->data = ap->contents->data;
                    // from ahead this pointer MUST BE
                    // freed by del_hefesto_modio()
                    ap->contents->data = NULL;
                    if (ap->type == HEFESTO_VAR_TYPE_STRING ||
                        ap->type == HEFESTO_VAR_TYPE_INT) {
                        map->dtype = ap->type;
                    } else {
                        map->dtype = HEFESTO_VAR_TYPE_INT;
                    }
                    if (modio->args == NULL) {
                        modio->args = map;
                        mio_next = modio->args;
                    } else {
                        mio_next->next = map;
                        mio_next = mio_next->next;
                    }
                    map = map->next;
                }
                // and here everything can happens, one-two-three, go! <:S
                modfunc(&modio);
                handle_byref(modio, args, lo_vars, gl_vars);
                if (modio->ret != NULL) {
                    **otype = modio->rtype;
                    if (modio->rtype == HEFESTO_VAR_TYPE_STRING) {
                        res = (char *) hefesto_mloc(modio->rsize + 1);
                        memset(res, 0, modio->rsize + 1);
                        memcpy(res, modio->ret, modio->rsize);
                    } else {
                        res = (int *) hefesto_mloc(sizeof(int));
                        *(int *)res = *(int *)modio->ret;
                    }
                }
                del_hefesto_modio(modio);
            }
        }
        // okay, we're still alive ;)
        del_hefesto_var_list_ctx(args);
    } else {
        **otype = HEFESTO_VAR_TYPE_NONE;
    }

    return res;
}

static void handle_byref(struct hefesto_modio *modio,
                         hefesto_var_list_ctx *args,
                         hefesto_var_list_ctx **lo_vars,
                         hefesto_var_list_ctx **gl_vars) {

    hefesto_var_list_ctx *ap, *vp;
    struct hefesto_modio_args *map;
    size_t osz;

    // handling byref requests from module
    ap = args->next->next;
    for (map = modio->args; map; map = map->next, ap = ap->next) {
        if (map->byref_on_return) {
            vp = get_hefesto_var_list_ctx_name(ap->name + 1, *lo_vars);
            if (vp == NULL) {
                vp = get_hefesto_var_list_ctx_name(ap->name + 1, *gl_vars);
            }
            if (vp == NULL) {
                hlsc_info(HLSCM_MTYPE_RUNTIME,
                          HLSCM_RUNTIME_ERROR_BYREF_UNK_SRC_VAR,
                          ap->name + 1);
            } else {
                if (vp->type == map->dtype) {
                    switch (vp->type) {

                        case HEFESTO_VAR_TYPE_STRING:
                        case HEFESTO_VAR_TYPE_INT:
                            if (map->data) {
                                del_hefesto_common_list_ctx(
                                                       vp->contents);
                                if (vp->type == HEFESTO_VAR_TYPE_STRING)
                                {
                                    osz = map->dsize;
                                } else {
                                    osz = sizeof(int);
                                }
                                vp->contents = NULL;
                                vp->contents =
                                      add_data_to_hefesto_common_list_ctx(
                                                       vp->contents,
                                                       map->data,
                                                       osz);
                            } else {
                                hlsc_info(HLSCM_MTYPE_RUNTIME,
                                    HLSCM_RUNTIME_ERROR_BYREF_UNINITZD_SRC_VAR,
                                    ap->name);
                            }
                            break;

                            default:
                                break;

                    }
                } else {
                    hlsc_info(HLSCM_MTYPE_RUNTIME,
                              HLSCM_RUNTIME_ERROR_BYREF_TYPE_MISMATCH,
                              ap->name, "(returned from module)");
                }
            }
        }
    }
}
