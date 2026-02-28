#include "alir.h"

void alir_fprint_type(FILE *f, VarType t) {
    switch(t.base) {
        case TYPE_INT: fprintf(f, "int"); break;
        case TYPE_LONG: fprintf(f, "long"); break;
        case TYPE_FLOAT: fprintf(f, "float"); break;
        case TYPE_DOUBLE: fprintf(f, "double"); break;
        case TYPE_CHAR: fprintf(f, "char"); break;
        case TYPE_BOOL: fprintf(f, "bool"); break;
        case TYPE_VOID: fprintf(f, "void"); break;
        case TYPE_STRING: fprintf(f, "string"); break;
        case TYPE_CLASS: fprintf(f, "%%%s", t.class_name ? t.class_name : "obj"); break;
        case TYPE_ENUM: fprintf(f, "int"); break;
        case TYPE_LONG_LONG: fprintf(f, "ll"); break;
        case TYPE_LONG_DOUBLE: fprintf(f, "ld"); break;
        case TYPE_ARRAY: fprintf(f, "array"); break;
        case TYPE_VECTOR: fprintf(f, "vec"); break;
        case TYPE_HASHMAP: fprintf(f, "hashmap"); break;
        case TYPE_AUTO: fprintf(f, "any"); break;
        case TYPE_UNKNOWN: fprintf(f, "unknown"); break;

        // TODO add ll, ld, array, vector, hashmap, auto, unknown
        default: fprintf(f, "def"); break;
    }
    // [FIX] Correct pointer depth logic. 
    // It used to evaluate `if(0 - 1)` which is TRUE, printing 'ptr' for 0 depth.
    if (t.ptr_depth > 0) fprintf(f, " ptr");
    for(int i=1; i<t.ptr_depth; i++) fprintf(f, "*");
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
            // [FIX] ALIR_VAL_VAR must map to local parameters/registers, not globals
            fprintf(f, "%%%s", v->str_val);
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

void alir_emit_function(AlirModule *mod, FILE *f) {
  AlirFunction *func = mod->functions;
  while(func) {
      if (func->block_count == 0) {
          fprintf(f, "\npromise ");
          alir_fprint_type(f, func->ret_type);
          fprintf(f, " @%s(", func->name);
          
          AlirParam *p = func->params;
          while(p) {
              alir_fprint_type(f, p->type);
              if (p->next) fprintf(f, ", ");
              p = p->next;
          }
          if (func->is_varargs) {
              if (func->param_count > 0) fprintf(f, ", ");
              fprintf(f, "...");
          }
          fprintf(f, ")\n");
          
      } else {
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
          if (func->is_varargs) {
              if (func->param_count > 0) fprintf(f, ", ");
              fprintf(f, "...");
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
                  
                  if (inst->op == ALIR_OP_ALLOCA && inst->dest) {
                      fprintf(f, "onstack ");
                      alir_fprint_type(f, inst->dest->type);
                  } 
                  else {
                      // [FIX] Add required typing to the instruction output
                      fprintf(f, "%s ", alir_op_str(inst->op));
                      
                      if (inst->dest && inst->op != ALIR_OP_STORE && inst->op != ALIR_OP_CALL) {
                          alir_fprint_type(f, inst->dest->type);
                          fprintf(f, " ");
                      } else if (inst->op == ALIR_OP_STORE && inst->op1) {
                          alir_fprint_type(f, inst->op1->type);
                          fprintf(f, " ");
                      } else if (inst->op == ALIR_OP_RET && inst->op1) {
                          alir_fprint_type(f, inst->op1->type);
                          fprintf(f, " ");
                      }
                      
                      if (inst->op1) {
                          alir_fprint_val(f, inst->op1);
                      } else if (inst->op == ALIR_OP_STORE) {
                           fprintf(f, "undef"); 
                      } else if (inst->op == ALIR_OP_RET && !inst->op1) {
                          fprintf(f, "void");
                      }

                      if (inst->op == ALIR_OP_SWITCH) {
                          fprintf(f, " {");
                          AlirSwitchCase *c = inst->cases;
                          while(c) {
                              fprintf(f, " %ld: %s, ", c->value, c->label);
                              c = c->next;
                          }
                          fprintf(f, "} default ");
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
void alir_emit_stream(AlirModule *mod, FILE *f) {
    fprintf(f, "; Module: %s\n", mod->name);
    
    if (mod->enums) {
        fprintf(f, "\n; Enum Definitions\n");
        AlirEnum *e = mod->enums;
        while(e) {
            fprintf(f, "; enum %s { ", e->name);
            AlirEnumEntry *ent = e->entries;
            while(ent) {
                fprintf(f, "%s=%ld", ent->name, ent->value);
                if (ent->next) fprintf(f, ", ");
                ent = ent->next;
            }
            fprintf(f, " }\n");
            e = e->next;
        }
    }

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

    if (mod->globals) {
        fprintf(f, "\n; Globals\n");
        AlirGlobal *g = mod->globals;
        while(g) {
            char *esc = escape_string(g->string_content);
            if (g->type.base == TYPE_STRING) {
                fprintf(f, "@%s = string \"%s\"\n", g->name, esc);
            } else {
                fprintf(f, "@%s = cstring \"%s\"\n", g->name, esc);
            }
            // escape string does not use arena
            free(esc);
            g = g->next;
        }
    }
    
    alir_emit_function(mod, f); 
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
