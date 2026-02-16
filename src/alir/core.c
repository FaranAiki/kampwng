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

AlirBlock* alir_add_block(AlirFunction *func, const char *label_hint) {
    AlirBlock *b = calloc(1, sizeof(AlirBlock));
    static int global_id = 0;
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

AlirValue* alir_val_label(const char *label) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_LABEL;
    v->str_val = strdup(label);
    return v;
}

const char* alir_op_str(AlirOpcode op) {
    switch(op) {
        case ALIR_OP_ALLOCA: return "alloca";
        case ALIR_OP_STORE: return "store";
        case ALIR_OP_LOAD: return "load";
        case ALIR_OP_GET_PTR: return "getptr";
        case ALIR_OP_BITCAST: return "bitcast";
        
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
        case ALIR_OP_YIELD: return "yield";
        
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

// Changed to accept FILE*
static void alir_fprint_val(FILE *f, AlirValue *v) {
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
        case ALIR_VAL_LABEL:
            fprintf(f, "%s", v->str_val);
            break;
        default: fprintf(f, "?"); break;
    }
}

// Internal stream emitter
static void alir_emit_stream(AlirModule *mod, FILE *f) {
    fprintf(f, "; Module: %s\n", mod->name);
    
    AlirFunction *func = mod->functions;
    while(func) {
        fprintf(f, "\ndefine %s %s() {\n", func->is_flux ? "flux" : "func", func->name);
        
        AlirBlock *b = func->blocks;
        while(b) {
            fprintf(f, "%s:\n", b->label);
            
            AlirInst *i = b->head;
            while(i) {
                fprintf(f, "  ");
                if (i->dest) {
                    alir_fprint_val(f, i->dest);
                    fprintf(f, " = ");
                }
                
                fprintf(f, "%s ", alir_op_str(i->op));
                
                if (i->op1) alir_fprint_val(f, i->op1);
                
                if (i->op == ALIR_OP_SWITCH) {
                    fprintf(f, " [");
                    AlirSwitchCase *c = i->cases;
                    while(c) {
                        fprintf(f, " %ld: %s ", c->value, c->label);
                        c = c->next;
                    }
                    fprintf(f, "] else ");
                    if (i->op2) alir_fprint_val(f, i->op2);
                } else {
                    if (i->op2) {
                        fprintf(f, ", ");
                        alir_fprint_val(f, i->op2);
                    }
                    
                    if (i->args) {
                        fprintf(f, " (");
                        for(int k=0; k<i->arg_count; k++) {
                            if (k > 0) fprintf(f, ", ");
                            alir_fprint_val(f, i->args[k]);
                        }
                        fprintf(f, ")");
                    }
                }
                
                fprintf(f, "\n");
                i = i->next;
            }
            b = b->next;
        }
        fprintf(f, "}\n");
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
