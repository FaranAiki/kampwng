#include "codegen.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char* format_string(const char* input) {
  if (!input) return NULL;
  size_t len = strlen(input);
  char *new_str = malloc(len + 1);
  strcpy(new_str, input);
  return new_str;
}

void get_type_name(VarType t, char *buf) {
    char base_buf[128] = "";
    if (t.is_unsigned) strcpy(base_buf, "unsigned ");

    if (t.is_func_ptr) {
        // Recursively get return type name
        char ret_buf[64];
        get_type_name(*t.fp_ret_type, ret_buf);
        sprintf(base_buf, "%s (*)(", ret_buf);
        
        for (int i=0; i<t.fp_param_count; i++) {
            char p_buf[64];
            get_type_name(t.fp_param_types[i], p_buf);
            strcat(base_buf, p_buf);
            if (i < t.fp_param_count - 1) strcat(base_buf, ", ");
        }
        if (t.fp_is_varargs) {
            if (t.fp_param_count > 0) strcat(base_buf, ", ");
            strcat(base_buf, "...");
        }
        strcat(base_buf, ")");
        strcpy(buf, base_buf);
        return;
    }

    switch (t.base) {
        case TYPE_INT: strcat(base_buf, "int"); break;
        case TYPE_SHORT: strcat(base_buf, "short"); break;
        case TYPE_LONG: strcat(base_buf, "long"); break;
        case TYPE_LONG_LONG: strcat(base_buf, "long long"); break;
        case TYPE_CHAR: strcat(base_buf, "char"); break;
        case TYPE_BOOL: strcat(base_buf, "bool"); break;
        case TYPE_FLOAT: strcat(base_buf, "float"); break;
        case TYPE_DOUBLE: strcat(base_buf, "double"); break;
        case TYPE_LONG_DOUBLE: strcat(base_buf, "long double"); break;
        case TYPE_VOID: strcat(base_buf, "void"); break;
        case TYPE_STRING: strcat(base_buf, "string"); break;
        case TYPE_CLASS: 
            strcat(base_buf, t.class_name ? t.class_name : "object"); 
            break;
        default: strcat(base_buf, "unknown"); break;
    }
    
    strcpy(buf, base_buf);
    
    // Pointers
    for(int i=0; i<t.ptr_depth; i++) strcat(buf, "*");
    
    // Arrays
    if (t.array_size > 0) {
        char tmp[32];
        sprintf(tmp, "[%d]", t.array_size);
        strcat(buf, tmp);
    }
}

VarType codegen_calc_type(CodegenCtx *ctx, ASTNode *node) {
    VarType vt = {TYPE_UNKNOWN, 0, NULL, 0, 0};
    if (!node) return vt;
    
    switch (node->type) {
        case NODE_CAST:
            return ((CastNode*)node)->var_type;
            
        case NODE_LITERAL:
            return ((LiteralNode*)node)->var_type;
            
        case NODE_ARRAY_LIT: {
            ArrayLitNode *an = (ArrayLitNode*)node;
            VarType t = {TYPE_INT, 0, NULL, 0, 0};
            if (an->elements) t = codegen_calc_type(ctx, an->elements);
            
            int count = 0; ASTNode *el = an->elements; while(el){count++; el=el->next;}
            
            if (t.array_size > 0) {
                t.array_size = 0;
                t.ptr_depth++;
            }
            t.array_size = count;
            return t;
        }

        case NODE_VAR_REF: {
            Symbol *s = find_symbol(ctx, ((VarRefNode*)node)->name);
            if (s) return s->vtype;
            
            EnumInfo *ei = find_enum(ctx, ((VarRefNode*)node)->name);
            if (ei) return (VarType){TYPE_INT, 0, NULL, 0, 0}; 

            Symbol *this_sym = find_symbol(ctx, "this");
            if (this_sym && this_sym->vtype.class_name) {
                 ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
                 if (ci) {
                     VarType mvt;
                     if (get_member_index(ci, ((VarRefNode*)node)->name, NULL, &mvt) != -1) {
                         return mvt;
                     }
                 }
            }
            break;
        }
        
        case NODE_ARRAY_ACCESS: {
            ArrayAccessNode *aa = (ArrayAccessNode*)node;
            if (aa->target->type == NODE_VAR_REF) {
                EnumInfo *ei = find_enum(ctx, ((VarRefNode*)aa->target)->name);
                if (ei) return (VarType){TYPE_STRING, 0, NULL, 0, 0};
            }

            VarType t = codegen_calc_type(ctx, aa->target);
            if (t.array_size > 0) {
                t.array_size = 0; 
            } else if (t.ptr_depth > 0) {
                t.ptr_depth--;
            }
            return t;
        }

        case NODE_TRAIT_ACCESS: {
            TraitAccessNode *ta = (TraitAccessNode*)node;
            VarType t = codegen_calc_type(ctx, ta->object);
            if (t.base == TYPE_CLASS) {
                t.class_name = strdup(ta->trait_name);
                if (t.ptr_depth == 0) t.ptr_depth = 1;
                return t;
            }
            break;
        }

        case NODE_MEMBER_ACCESS: {
            MemberAccessNode *ma = (MemberAccessNode*)node;
            if (ma->object->type == NODE_VAR_REF) {
                EnumInfo *ei = find_enum(ctx, ((VarRefNode*)ma->object)->name);
                if (ei) return (VarType){TYPE_INT, 0, NULL, 0, 0};
            }

            VarType obj_t = codegen_calc_type(ctx, ma->object);
            if (obj_t.base == TYPE_CLASS && obj_t.class_name) {
                ClassInfo *ci = find_class(ctx, obj_t.class_name);
                if (ci) {
                     VarType mvt;
                     if (get_member_index(ci, ma->member_name, NULL, &mvt) != -1) {
                         return mvt;
                     }
                 }
            }
            break;
        }
        
        case NODE_CALL: {
            CallNode *call = (CallNode*)node;
            const char *name = call->mangled_name ? call->mangled_name : call->name;
            if (!name) return vt;
            
            FuncSymbol *fs = find_func_symbol(ctx, name);
            if (fs) return fs->ret_type;
            
            Symbol *var_sym = find_symbol(ctx, name);
            if (var_sym && var_sym->vtype.is_func_ptr) {
                return *var_sym->vtype.fp_ret_type;
            }

            ClassInfo *ci = find_class(ctx, call->name); 
            if (ci) {
                vt.base = TYPE_CLASS;
                vt.class_name = strdup(call->name);
                vt.ptr_depth = 1; 
                return vt;
            }

            if (strcmp(name, "input") == 0) { vt.base = TYPE_STRING; return vt; }
            break;
        }

        case NODE_METHOD_CALL: {
            MethodCallNode *mc = (MethodCallNode*)node;
            if (mc->mangled_name) {
                 FuncSymbol *fs = find_func_symbol(ctx, mc->mangled_name);
                 if (fs) return fs->ret_type;
            }
            break;
        }
        
        case NODE_TYPEOF:
            return (VarType){TYPE_STRING, 0, NULL, 0, 0};
        
        case NODE_HAS_METHOD:
        case NODE_HAS_ATTRIBUTE: {
            VarType ret = {TYPE_STRING, 0, NULL, 0, 0};
            ret.ptr_depth = 1; 
            return ret;
        }
        
        case NODE_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode*)node;
            VarType t = codegen_calc_type(ctx, u->operand);
            if (u->op == TOKEN_AND) { t.ptr_depth++; return t; }
            if (u->op == TOKEN_STAR) { if (t.ptr_depth > 0) t.ptr_depth--; return t; }
            if (u->op == TOKEN_NOT) { return (VarType){TYPE_BOOL, 0, NULL, 0, 0}; }
            return t; 
        }

        case NODE_BINARY_OP: {
            BinaryOpNode *op = (BinaryOpNode*)node;
            
            if (op->op == TOKEN_EQ || op->op == TOKEN_NEQ ||
                op->op == TOKEN_LT || op->op == TOKEN_GT ||
                op->op == TOKEN_LTE || op->op == TOKEN_GTE) {
                return (VarType){TYPE_BOOL, 0, NULL, 0, 0};
            }

            VarType l = codegen_calc_type(ctx, op->left);
            VarType r = codegen_calc_type(ctx, op->right);
            
            if (op->op == TOKEN_PLUS && (l.base == TYPE_STRING || r.base == TYPE_STRING)) {
                 return (VarType){TYPE_STRING, 0, NULL, 0, 0};
            }
            
            if (l.base == TYPE_LONG_DOUBLE || r.base == TYPE_LONG_DOUBLE) return (VarType){TYPE_LONG_DOUBLE, 0, NULL, 0, 0};
            if (l.base == TYPE_DOUBLE || r.base == TYPE_DOUBLE) return (VarType){TYPE_DOUBLE, 0, NULL, 0, 0};
            if (l.base == TYPE_FLOAT || r.base == TYPE_FLOAT) return (VarType){TYPE_FLOAT, 0, NULL, 0, 0};
            
            if (l.base == TYPE_LONG_LONG || r.base == TYPE_LONG_LONG) return (VarType){TYPE_LONG_LONG, 0, NULL, 0, 0};
            if (l.base == TYPE_LONG || r.base == TYPE_LONG) return (VarType){TYPE_LONG, 0, NULL, 0, 0};
            
            return l;
        }
        default: break;
    }
    
    return vt;
}
