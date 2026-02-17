#include "alir.h"
#include <stdlib.h>
#include <string.h>

AlirModule* alir_create_module(const char *name) {
    AlirModule *m = calloc(1, sizeof(AlirModule));
    m->name = strdup(name);
    return m;
}

AlirFunction* alir_add_function(AlirModule *mod, const char *name, VarType ret, int is_flux) {
    AlirFunction *f = calloc(1, sizeof(AlirFunction));
    f->name = strdup(name);
    f->ret_type = ret;
    f->is_flux = is_flux;
    
    if (!mod->functions) {
        mod->functions = f;
    } else {
        AlirFunction *curr = mod->functions;
        while(curr->next) curr = curr->next;
        curr->next = f;
    }
    return f;
}

void alir_func_add_param(AlirFunction *func, const char *name, VarType type) {
    AlirParam *p = calloc(1, sizeof(AlirParam));
    p->name = strdup(name ? name : "");
    p->type = type;
    
    if (!func->params) {
        func->params = p;
    } else {
        AlirParam *curr = func->params;
        while(curr->next) curr = curr->next;
        curr->next = p;
    }
    func->param_count++;
}

// Add string to global pool
AlirValue* alir_module_add_string_literal(AlirModule *mod, const char *content, int id_hint) {
    char label[64];
    sprintf(label, "str.%d", id_hint);
    
    AlirGlobal *g = calloc(1, sizeof(AlirGlobal));
    g->name = strdup(label);
    g->string_content = strdup(content);
    g->type = (VarType){TYPE_STRING, 0, NULL, 0, 0};
    
    g->next = mod->globals;
    mod->globals = g;
    
    return alir_val_global(label, g->type);
}

AlirBlock* alir_add_block(AlirFunction *func, const char *label_hint) {
    AlirBlock *b = calloc(1, sizeof(AlirBlock));
    int global_id = 0;
    b->id = global_id++;
    
    if (label_hint) b->label = strdup(label_hint);
    else {
        char buf[32];
        sprintf(buf, "L%d", b->id);
        b->label = strdup(buf);
    }

    if (!func->blocks) {
        func->blocks = b;
    } else {
        AlirBlock *curr = func->blocks;
        while(curr->next) curr = curr->next;
        curr->next = b;
    }
    func->block_count++;
    return b;
}

void alir_append_inst(AlirBlock *block, AlirInst *inst) {
    if (!block->head) {
        block->head = inst;
        block->tail = inst;
    } else {
        block->tail->next = inst;
        block->tail = inst;
    }
}

// --- STRUCT REGISTRY ---

void alir_register_struct(AlirModule *mod, const char *name, AlirField *fields) {
    AlirStruct *st = calloc(1, sizeof(AlirStruct));
    st->name = strdup(name);
    st->fields = fields;
    
    AlirField *f = fields;
    while(f) {
        st->field_count++;
        f = f->next;
    }
    
    st->next = mod->structs;
    mod->structs = st;
}

AlirStruct* alir_find_struct(AlirModule *mod, const char *name) {
    AlirStruct *curr = mod->structs;
    while(curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

int alir_get_field_index(AlirModule *mod, const char *struct_name, const char *field_name) {
    AlirStruct *st = alir_find_struct(mod, struct_name);
    if (!st) return -1;
    
    AlirField *f = st->fields;
    while(f) {
        if (strcmp(f->name, field_name) == 0) return f->index;
        f = f->next;
    }
    return -1;
}

// --- VALUE CREATORS ---

AlirValue* alir_const_int(long val) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_CONST;
    v->type = (VarType){TYPE_INT, 0, NULL, 0, 0};
    v->int_val = val;
    return v;
}

AlirValue* alir_const_float(double val) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_CONST;
    v->type = (VarType){TYPE_DOUBLE, 0, NULL, 0, 0};
    v->float_val = val;
    return v;
}

AlirValue* alir_val_temp(VarType t, int id) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_TEMP;
    v->type = t;
    v->temp_id = id;
    return v;
}

AlirValue* alir_val_var(const char *name) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_VAR;
    v->str_val = strdup(name);
    return v;
}

AlirValue* alir_val_global(const char *name, VarType type) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_GLOBAL;
    v->str_val = strdup(name);
    v->type = type;
    return v;
}

AlirValue* alir_val_label(const char *label) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_LABEL;
    v->str_val = strdup(label);
    return v;
}

AlirValue* alir_val_type(const char *type_name) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_TYPE;
    v->str_val = strdup(type_name);
    v->type = (VarType){TYPE_CLASS, 0, strdup(type_name), 0, 0};
    return v;
}

const char* alir_op_str(AlirOpcode op) {
    switch(op) {
        case ALIR_OP_ALLOCA: return "alloca";
        case ALIR_OP_STORE: return "store";
        case ALIR_OP_LOAD: return "load";
        case ALIR_OP_GET_PTR: return "getptr";
        case ALIR_OP_BITCAST: return "bitcast";
        
        case ALIR_OP_ALLOC_HEAP: return "alloc_heap";
        case ALIR_OP_SIZEOF: return "sizeof";
        case ALIR_OP_FREE: return "free";
        
        case ALIR_OP_ADD: return "add";
        case ALIR_OP_SUB: return "sub";
        case ALIR_OP_MUL: return "mul";
        case ALIR_OP_DIV: return "div";
        case ALIR_OP_MOD: return "mod";
        case ALIR_OP_FADD: return "fadd";
        case ALIR_OP_FSUB: return "fsub";
        case ALIR_OP_FMUL: return "fmul";
        case ALIR_OP_FDIV: return "fdiv";
        
        case ALIR_OP_BR: return "br";
        case ALIR_OP_COND_BR: return "br.cond";
        case ALIR_OP_SWITCH: return "switch";
        case ALIR_OP_CALL: return "call";
        case ALIR_OP_RET: return "ret";
        
        // Added flux/iterator support strings
        case ALIR_OP_YIELD: return "yield";
        case ALIR_OP_ITER_INIT: return "iter_init";
        case ALIR_OP_ITER_VALID: return "iter_valid";
        case ALIR_OP_ITER_NEXT: return "iter_next";
        case ALIR_OP_ITER_GET: return "iter_get";
        
        case ALIR_OP_CAST: return "cast";
        case ALIR_OP_NOT: return "not";
        
        case ALIR_OP_LT: return "lt";
        case ALIR_OP_GT: return "gt";
        case ALIR_OP_LTE: return "lte";
        case ALIR_OP_GTE: return "gte";
        case ALIR_OP_EQ: return "eq";
        case ALIR_OP_NEQ: return "neq";
        
        case ALIR_OP_AND: return "and";
        case ALIR_OP_OR: return "or";
        case ALIR_OP_XOR: return "xor";
        case ALIR_OP_SHL: return "shl";
        case ALIR_OP_SHR: return "shr";
        
        default: return "op";
    }
}

char* escape_string(const char* input) {
    if (!input) return strdup("");
    int len = strlen(input);
    char* out = malloc(len * 2 + 1); // Worst case
    char* p = out;
    for (int i = 0; i < len; i++) {
        if (input[i] == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else if (input[i] == '\t') { *p++ = '\\'; *p++ = 't'; }
        else if (input[i] == '\"') { *p++ = '\\'; *p++ = '\"'; }
        else if (input[i] == '\\') { *p++ = '\\'; *p++ = '\\'; }
        else *p++ = input[i];
    }
    *p = '\0';
    return out;
}

void alir_fprint_type(FILE *f, VarType t) {
    switch(t.base) {
        case TYPE_INT: fprintf(f, "i32"); break;
        case TYPE_LONG: fprintf(f, "i64"); break;
        case TYPE_FLOAT: fprintf(f, "float"); break;
        case TYPE_DOUBLE: fprintf(f, "double"); break;
        case TYPE_CHAR: fprintf(f, "i8"); break;
        case TYPE_BOOL: fprintf(f, "i1"); break;
        case TYPE_VOID: fprintf(f, "void"); break;
        case TYPE_STRING: fprintf(f, "string"); break;
        case TYPE_CLASS: fprintf(f, "%%%s", t.class_name ? t.class_name : "obj"); break;
        default: fprintf(f, "any"); break;
    }
    for(int i=0; i<t.ptr_depth; i++) fprintf(f, "*");
}

void alir_fprint_val(FILE *f, AlirValue *v) {
    if (!v) { fprintf(f, "void"); return; }
    switch(v->kind) {
        case ALIR_VAL_CONST:
            if (v->type.base == TYPE_FLOAT || v->type.base == TYPE_DOUBLE)
                fprintf(f, "%.2f", v->float_val);
            else
                fprintf(f, "%ld", v->int_val);
            break;
        case ALIR_VAL_TEMP:
            fprintf(f, "%%%d", v->temp_id);
            break;
        case ALIR_VAL_VAR:
            fprintf(f, "@%s", v->str_val);
            break;
        case ALIR_VAL_GLOBAL:
            fprintf(f, "@%s", v->str_val);
            break;
        case ALIR_VAL_LABEL:
            fprintf(f, "%s", v->str_val);
            break;
        case ALIR_VAL_TYPE:
            fprintf(f, "type(%s)", v->str_val);
            break;
        default: fprintf(f, "?"); break;
    }
}

// Internal stream emitter
void alir_emit_stream(AlirModule *mod, FILE *f) {
    fprintf(f, "; Module: %s\n", mod->name);
    
    // 1. Print Structs
    if (mod->structs) {
        fprintf(f, "\n; Struct Definitions\n");
        AlirStruct *st = mod->structs;
        while(st) {
            fprintf(f, "%%struct.%s = type { ", st->name);
            AlirField *fd = st->fields;
            while(fd) {
                alir_fprint_type(f, fd->type);
                fprintf(f, ":%s", fd->name);
                if (fd->next) fprintf(f, ", ");
                fd = fd->next;
            }
            fprintf(f, " }\n");
            st = st->next;
        }
    }

    // 2. Print Globals (Strings)
    if (mod->globals) {
        fprintf(f, "\n; Globals\n");
        AlirGlobal *g = mod->globals;
        while(g) {
            char *esc = escape_string(g->string_content);
            fprintf(f, "@%s = constant string \"%s\"\n", g->name, esc);
            free(esc);
            g = g->next;
        }
    }
    
    // 3. Print Functions
    AlirFunction *func = mod->functions;
    while(func) {
        if (func->block_count == 0) {
            // Declaration
            fprintf(f, "\ndeclare ");
            alir_fprint_type(f, func->ret_type);
            fprintf(f, " @%s(", func->name);
            
            AlirParam *p = func->params;
            while(p) {
                alir_fprint_type(f, p->type);
                if (p->next) fprintf(f, ", ");
                p = p->next;
            }
            fprintf(f, ")\n");
            
        } else {
            // Definition
            fprintf(f, "\ndefine %s ", func->is_flux ? "flux" : "func");
            alir_fprint_type(f, func->ret_type);
            fprintf(f, " @%s(", func->name);
            
            AlirParam *p = func->params;
            int i = 0;
            while(p) {
                alir_fprint_type(f, p->type);
                fprintf(f, " %%p%d", i++);
                if (p->next) fprintf(f, ", ");
                p = p->next;
            }
            fprintf(f, ") {\n");
            
            AlirBlock *b = func->blocks;
            while(b) {
                fprintf(f, "%s:\n", b->label);
                
                AlirInst *inst = b->head;
                while(inst) {
                    fprintf(f, "  ");
                    if (inst->dest) {
                        alir_fprint_val(f, inst->dest);
                        fprintf(f, " = ");
                    }
                    
                    // Special handling for ALLOCA to print type
                    if (inst->op == ALIR_OP_ALLOCA && inst->dest) {
                        fprintf(f, "alloca ");
                        // dest is assumed to be the variable type directly in current gen logic
                        // If logic implies dest is pointer, we should handle that, but for now
                        // we print the type stored in the temp info
                        alir_fprint_type(f, inst->dest->type);
                    } 
                    else {
                        fprintf(f, "%s ", alir_op_str(inst->op));
                        
                        if (inst->op1) {
                            alir_fprint_val(f, inst->op1);
                        } else if (inst->op == ALIR_OP_STORE) {
                             // Handle missing value in store (e.g. uninit var)
                             fprintf(f, "undef"); 
                        } else if (inst->op == ALIR_OP_RET && !inst->op1) {
                            fprintf(f, "void");
                        }

                        if (inst->op == ALIR_OP_SWITCH) {
                            fprintf(f, " [");
                            AlirSwitchCase *c = inst->cases;
                            while(c) {
                                fprintf(f, " %ld: %s ", c->value, c->label);
                                c = c->next;
                            }
                            fprintf(f, "] else ");
                            if (inst->op2) alir_fprint_val(f, inst->op2);
                        } else {
                            if (inst->op2) {
                                fprintf(f, ", ");
                                alir_fprint_val(f, inst->op2);
                            }
                            
                            if (inst->args) {
                                fprintf(f, " (");
                                for(int k=0; k<inst->arg_count; k++) {
                                    if (k > 0) fprintf(f, ", ");
                                    alir_fprint_val(f, inst->args[k]);
                                }
                                fprintf(f, ")");
                            }
                        }
                    }
                    
                    fprintf(f, "\n");
                    inst = inst->next;
                }
                b = b->next;
            }
            fprintf(f, "}\n");
        }
        func = func->next;
    }
}

void alir_print(AlirModule *mod) {
    alir_emit_stream(mod, stdout);
}

void alir_emit_to_file(AlirModule *mod, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open file for ALIR output");
        return;
    }
    alir_emit_stream(mod, f);
    fclose(f);
}
