#include "alir.h"

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
        case TYPE_ENUM: fprintf(f, "i32"); break; // Enums degrade to i32 in IR
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

void alir_emit_function(AlirModule *mod, FILE *f) {
  AlirFunction *func = mod->functions;
  while(func) {
      if (func->block_count == 0) {
          // Declaration
          fprintf(f, "\npromise ");
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
            fprintf(f, "@%s = cstring \"%s\"\n", g->name, esc);
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
