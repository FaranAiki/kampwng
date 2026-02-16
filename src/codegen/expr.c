#include "codegen.h"
#include "../diagnostic/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LLVMTypeRef get_lvalue_struct_type(CodegenCtx *ctx, ASTNode *node) {
    if (!node) return NULL;

    if (node->type == NODE_VAR_REF) {
        Symbol *sym = find_symbol(ctx, ((VarRefNode*)node)->name);
        if (sym) return sym->type;

        Symbol *this_sym = find_symbol(ctx, "this");
        if (this_sym && this_sym->vtype.class_name) {
             ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
             if (ci) {
                 LLVMTypeRef mtype;
                 if (get_member_index(ci, ((VarRefNode*)node)->name, &mtype, NULL) != -1) {
                     return mtype;
                 }
             }
        }
        return NULL;
    }

    if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *aa = (ArrayAccessNode*)node;
        LLVMTypeRef parent_t = get_lvalue_struct_type(ctx, aa->target);
        if (!parent_t) return NULL;

        if (LLVMGetTypeKind(parent_t) == LLVMArrayTypeKind) {
            return LLVMGetElementType(parent_t);
        }
        return NULL;
    }

    if (node->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)node;
        VarType obj_vt = codegen_calc_type(ctx, ma->object);
        if (obj_vt.class_name) {
            ClassInfo *ci = find_class(ctx, obj_vt.class_name);
            LLVMTypeRef mtype;
            if (ci && get_member_index(ci, ma->member_name, &mtype, NULL) != -1) {
                return mtype;
            }
        }
        return NULL;
    }

    return NULL;
}

// Calculate the Alkyl VarType of an expression
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

// Generate the memory address (l-value) of an expression
LLVMValueRef codegen_addr(CodegenCtx *ctx, ASTNode *node) {
    if (!node) return NULL;

    switch(node->type) {
        case NODE_VAR_REF: {
            VarRefNode *r = (VarRefNode*)node;
            Symbol *sym = find_symbol(ctx, r->name);
            
            if (sym && sym->is_direct_value) return NULL;
            if (sym) return sym->value;
            
            Symbol *this_sym = find_symbol(ctx, "this");
            if (this_sym && this_sym->vtype.class_name) {
                 ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
                 if (ci) {
                     int idx = get_member_index(ci, r->name, NULL, NULL);
                     if (idx != -1) {
                         LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder, this_sym->type, this_sym->value, "this_ptr");
                         LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                         return LLVMBuildGEP2(ctx->builder, ci->struct_type, this_val, indices, 2, "implicit_mem_ptr");
                     }
                 }
            }
            break;
        }

        case NODE_ARRAY_ACCESS: {
             ArrayAccessNode *aa = (ArrayAccessNode*)node;
             if (aa->target->type == NODE_VAR_REF) {
                  if (find_enum(ctx, ((VarRefNode*)aa->target)->name)) return NULL;
             }

             LLVMValueRef target = codegen_addr(ctx, aa->target);
             if (!target) return NULL;

             LLVMValueRef index = codegen_expr(ctx, aa->index);
             LLVMTypeRef target_struct_type = get_lvalue_struct_type(ctx, aa->target);
             
             if (target_struct_type && LLVMGetTypeKind(target_struct_type) == LLVMArrayTypeKind) {
                 LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), index };
                 return LLVMBuildGEP2(ctx->builder, target_struct_type, target, indices, 2, "arr_idx");
             } 
             
             VarType t = codegen_calc_type(ctx, aa->target);
             LLVMTypeRef llvm_t = get_llvm_type(ctx, t);
             
             if (t.ptr_depth > 0) {
                 LLVMValueRef base = LLVMBuildLoad2(ctx->builder, llvm_t, target, "ptr_base");
                 t.ptr_depth--;
                 LLVMTypeRef inner_type = get_llvm_type(ctx, t);
                 return LLVMBuildGEP2(ctx->builder, inner_type, base, &index, 1, "ptr_idx");
             }
             break;
        }

        case NODE_MEMBER_ACCESS: {
             MemberAccessNode *ma = (MemberAccessNode*)node;
             if (ma->object->type == NODE_VAR_REF) {
                  if (find_enum(ctx, ((VarRefNode*)ma->object)->name)) return NULL;
             }

             LLVMValueRef obj = codegen_addr(ctx, ma->object);
             VarType obj_t = codegen_calc_type(ctx, ma->object);
             
             if (obj_t.base == TYPE_CLASS && obj_t.class_name) {
                 ClassInfo *ci = find_class(ctx, obj_t.class_name);
                 if (ci) {
                     LLVMTypeRef member_type = NULL;
                     int idx = get_member_index(ci, ma->member_name, &member_type, NULL);
                     if (idx != -1) {
                         if (ci->is_union) {
                             LLVMValueRef base = obj;
                             if (obj_t.ptr_depth > 0) {
                                 if (obj) base = LLVMBuildLoad2(ctx->builder, LLVMPointerType(ci->struct_type, 0), obj, "obj_ptr");
                                 else base = codegen_expr(ctx, ma->object);
                             } else {
                                if (!obj) {
                                    codegen_error(ctx, node, "Cannot access union member of r-value");
                                }
                             }
                             return LLVMBuildBitCast(ctx->builder, base, LLVMPointerType(member_type, 0), "union_mem_ptr");
                         } else {
                            if (obj_t.ptr_depth > 0) {
                                LLVMValueRef base;
                                if (obj) base = LLVMBuildLoad2(ctx->builder, LLVMPointerType(ci->struct_type, 0), obj, "obj_ptr");
                                else base = codegen_expr(ctx, ma->object);
                                
                                LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                                return LLVMBuildGEP2(ctx->builder, ci->struct_type, base, indices, 2, "mem_ptr");
                            } else {
                                if (!obj) codegen_error(ctx, node, "Cannot access member of r-value struct");
                                
                                LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                                return LLVMBuildGEP2(ctx->builder, ci->struct_type, obj, indices, 2, "mem_ptr");
                            }
                         }
                     }
                 }
             }
             break;
        }

        case NODE_UNARY_OP: {
             UnaryOpNode *u = (UnaryOpNode*)node;
             if (u->op == TOKEN_STAR) {
                 return codegen_expr(ctx, u->operand);
             }
             break;
        }
        default: break;
    }
    
    return NULL;
}

// --- Main Expression Dispatcher ---

LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node) {
  if (!node) return LLVMConstInt(LLVMInt32Type(), 0, 0);

  switch (node->type) {
      case NODE_CALL: return gen_call(ctx, (CallNode*)node);
      case NODE_METHOD_CALL: return gen_method_call(ctx, (MethodCallNode*)node);
      case NODE_TRAIT_ACCESS: return gen_trait_access(ctx, (TraitAccessNode*)node);
      case NODE_ARRAY_LIT: return gen_array_lit(ctx, (ArrayLitNode*)node);
      case NODE_LITERAL: return gen_literal(ctx, (LiteralNode*)node);
      case NODE_BINARY_OP: return gen_binary_op(ctx, (BinaryOpNode*)node);
      case NODE_UNARY_OP: return gen_unary_op(ctx, (UnaryOpNode*)node);
      case NODE_ARRAY_ACCESS: return gen_array_access(ctx, (ArrayAccessNode*)node);
      case NODE_MEMBER_ACCESS: return gen_member_access(ctx, (MemberAccessNode*)node);
      case NODE_INC_DEC: return gen_inc_dec(ctx, (IncDecNode*)node);
      case NODE_VAR_REF: return gen_var_ref(ctx, (VarRefNode*)node);
      case NODE_TYPEOF: return gen_typeof(ctx, (UnaryOpNode*)node);
      case NODE_HAS_METHOD: return gen_reflection(ctx, (UnaryOpNode*)node, 1);
      case NODE_HAS_ATTRIBUTE: return gen_reflection(ctx, (UnaryOpNode*)node, 0);
      case NODE_CAST: return gen_cast(ctx, (CastNode*)node);
      default: return LLVMConstInt(LLVMInt32Type(), 0, 0);
  }
}

// todo is this good enough?
LLVMValueRef gen_call(CodegenCtx *ctx, CallNode *c) {
    if (!c->name) {
        codegen_error(ctx, (ASTNode*)c, "Function call with no name");
        return LLVMConstInt(LLVMInt32Type(), 0, 0);
    }

    ClassInfo *ci = find_class(ctx, c->name);
    if (ci) {
        // Constructor Logic
        LLVMValueRef size = LLVMSizeOf(ci->struct_type);
        LLVMValueRef mem = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "new_obj");
        LLVMValueRef obj = LLVMBuildBitCast(ctx->builder, mem, LLVMPointerType(ci->struct_type, 0), "obj_cast");
        
        ASTNode *arg = c->args;
        ClassMember *m = ci->members;
        
        if (ci->is_union) {
            if (arg) {
                 VarType r_vt = codegen_calc_type(ctx, arg);
                 LLVMValueRef arg_val = codegen_expr(ctx, arg);
                 
                 ClassMember *match = NULL;
                 ClassMember *iter = ci->members;
                 while(iter) {
                      if (iter->vtype.base == r_vt.base && iter->vtype.ptr_depth == r_vt.ptr_depth && iter->vtype.array_size == r_vt.array_size) {
                          match = iter; break;
                      }
                      if (iter->vtype.base == TYPE_CHAR && iter->vtype.array_size > 0 && 
                         (r_vt.base == TYPE_STRING || (r_vt.base == TYPE_CHAR && r_vt.ptr_depth == 1))) {
                          match = iter; break;
                      }
                      if (iter->vtype.base == r_vt.base && iter->vtype.ptr_depth == r_vt.ptr_depth) {
                          match = iter; break;
                      }
                      iter = iter->next;
                 }
                 
                 if (match) {
                      if (match->vtype.base == TYPE_CHAR && match->vtype.array_size > 0 && 
                         (r_vt.base == TYPE_STRING || (r_vt.base == TYPE_CHAR && r_vt.ptr_depth == 1))) {
                           LLVMValueRef mem_ptr = LLVMBuildBitCast(ctx->builder, obj, LLVMPointerType(match->type, 0), "union_init_ptr");
                           LLVMValueRef dest = LLVMBuildBitCast(ctx->builder, mem_ptr, LLVMPointerType(LLVMInt8Type(), 0), "dest_cast");
                           LLVMValueRef args[] = { dest, arg_val };
                           LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args, 2, "");
                      } else {
                           LLVMValueRef mem_ptr = LLVMBuildBitCast(ctx->builder, obj, LLVMPointerType(match->type, 0), "union_init_ptr");
                           LLVMBuildStore(ctx->builder, arg_val, mem_ptr);
                      }
                 }
            }
        } else {
             while (arg && m) {
                LLVMValueRef arg_val = codegen_expr(ctx, arg);
                LLVMValueRef mem_ptr;
                
                LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), m->index, 0) };
                mem_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, obj, indices, 2, "init_mem_ptr");
                
                if (m->vtype.array_size > 0 && m->vtype.base == TYPE_CHAR) {
                     LLVMValueRef dest = LLVMBuildBitCast(ctx->builder, mem_ptr, LLVMPointerType(LLVMInt8Type(), 0), "dest_cast");
                     LLVMValueRef src = arg_val;
                     LLVMValueRef args[] = { dest, src };
                     LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args, 2, "");
                } else {
                     LLVMBuildStore(ctx->builder, arg_val, mem_ptr);
                }
                
                arg = arg->next;
                m = m->next;
            }
        }
        return obj;
    }
    
    if (strcmp(c->name, "print") == 0 || strcmp(c->name, "printf") == 0) {
      int arg_count = 0; ASTNode *curr = c->args; while(curr) { arg_count++; curr = curr->next; }
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
      curr = c->args; for(int i=0; i<arg_count; i++) { args[i] = codegen_expr(ctx, curr); curr = curr->next; }
      LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, args, arg_count, "");
      free(args); return ret;
    }
    
    if (strcmp(c->name, "input") == 0) {
        return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->input_func), ctx->input_func, NULL, 0, "user_input");
    }
    
    if (strcmp(c->name, "malloc") == 0 || strcmp(c->name, "alloc") == 0) {
        LLVMValueRef size = codegen_expr(ctx, c->args);
        return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "malloc_res");
    }
    
    if (strcmp(c->name, "free") == 0) {
        LLVMValueRef ptr = codegen_expr(ctx, c->args);
        LLVMValueRef args[] = { LLVMBuildBitCast(ctx->builder, ptr, LLVMPointerType(LLVMInt8Type(), 0), "free_cast") };
        return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->free_func), ctx->free_func, args, 1, "");
    }

    Symbol *var_sym = find_symbol(ctx, c->name);
    if (var_sym && var_sym->vtype.is_func_ptr) {
        LLVMValueRef func_ptr = LLVMBuildLoad2(ctx->builder, var_sym->type, var_sym->value, "fp_load");
        
        int arg_count = 0; ASTNode *curr = c->args; while(curr) { arg_count++; curr = curr->next; }
        LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
        
        curr = c->args; 
        for(int i=0; i<arg_count; i++) { 
            args[i] = codegen_expr(ctx, curr); 
            curr = curr->next;
        }
        
        LLVMTypeRef ftype = get_llvm_type(ctx, var_sym->vtype);
        LLVMTypeRef func_sig = LLVMGetElementType(ftype);

        LLVMValueRef ret = LLVMBuildCall2(ctx->builder, func_sig, func_ptr, args, arg_count, "");
        free(args);
        return ret;
    }

    const char *target_name = c->mangled_name ? c->mangled_name : c->name;
    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, target_name);
    
    if (!func) {
        func = LLVMGetNamedFunction(ctx->module, c->name);
        if (!func) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Undefined function '%s'.", c->name);
            codegen_error(ctx, (ASTNode*)c, msg);
        }
    }
    
    FuncSymbol *fsym = find_func_symbol(ctx, target_name);
    if (!fsym) fsym = find_func_symbol(ctx, c->name);
    
    int arg_count = 0; ASTNode *curr = c->args; while(curr) { arg_count++; curr = curr->next; }
    LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
    curr = c->args; 
    
    for(int i=0; i<arg_count; i++) { 
        LLVMValueRef val = codegen_expr(ctx, curr);
        if (fsym && i < fsym->param_count) {
             VarType expected = fsym->param_types[i];
             LLVMTypeRef llvm_expected = get_llvm_type(ctx, expected);
             
             if (LLVMGetTypeKind(llvm_expected) == LLVMDoubleTypeKind) {
                 if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                     val = expected.is_unsigned ? LLVMBuildUIToFP(ctx->builder, val, llvm_expected, "cast") : LLVMBuildSIToFP(ctx->builder, val, llvm_expected, "cast");
                 } else if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMFloatTypeKind) {
                     val = LLVMBuildFPExt(ctx->builder, val, llvm_expected, "cast");
                 }
             } else if (LLVMGetTypeKind(llvm_expected) == LLVMFloatTypeKind) {
                 if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                     val = expected.is_unsigned ? LLVMBuildUIToFP(ctx->builder, val, llvm_expected, "cast") : LLVMBuildSIToFP(ctx->builder, val, llvm_expected, "cast");
                 }
             } else if (LLVMGetTypeKind(llvm_expected) == LLVMIntegerTypeKind) {
                 if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                     unsigned e_w = LLVMGetIntTypeWidth(llvm_expected);
                     unsigned v_w = LLVMGetIntTypeWidth(LLVMTypeOf(val));
                     if (e_w > v_w) val = expected.is_unsigned ? LLVMBuildZExt(ctx->builder, val, llvm_expected, "cast") : LLVMBuildSExt(ctx->builder, val, llvm_expected, "cast");
                     else if (v_w > e_w) val = LLVMBuildTrunc(ctx->builder, val, llvm_expected, "cast");
                 }
             }
        }
        args[i] = val; 
        curr = curr->next; 
    }
    
    LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
    LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count, "");
    free(args); return ret;
}

LLVMValueRef gen_method_call(CodegenCtx *ctx, MethodCallNode *mc) {
      if (mc->is_static) {
          if (!mc->mangled_name) codegen_error(ctx, (ASTNode*)mc, "call missing mangled name");
          
          LLVMValueRef func = LLVMGetNamedFunction(ctx->module, mc->mangled_name);
          if (!func) codegen_error(ctx, (ASTNode*)mc, "function not found in module");
          
          int arg_count = 0;
          ASTNode *arg = mc->args;
          while(arg) { arg_count++; arg = arg->next; }
          
          LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
          arg = mc->args;
          for(int i=0; i<arg_count; i++) { args[i] = codegen_expr(ctx, arg); arg = arg->next; }
          
          LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
          LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count, "");
          free(args);
          return ret;
      }
      
      LLVMValueRef obj_ptr = NULL;
      VarType obj_t = codegen_calc_type(ctx, mc->object);
      
      if (obj_t.ptr_depth > 0) obj_ptr = codegen_expr(ctx, mc->object);
      else obj_ptr = codegen_addr(ctx, mc->object);
      
      if (!obj_ptr) codegen_error(ctx, (ASTNode*)mc, "Invalid object for method call");
      
      if (mc->owner_class && obj_t.class_name && strcmp(mc->owner_class, obj_t.class_name) != 0) {
          ClassInfo *ci = find_class(ctx, obj_t.class_name);
          int offset = get_trait_offset(ctx, ci, mc->owner_class);
          if (offset != -1) {
              LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), offset, 0) };
              obj_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, obj_ptr, indices, 2, "trait_this_adj");
          } else {
              ClassInfo *target_cls = find_class(ctx, mc->owner_class);
              if (target_cls) {
                  obj_ptr = LLVMBuildBitCast(ctx->builder, obj_ptr, LLVMPointerType(target_cls->struct_type, 0), "parent_this_cast");
              }
          }
      }
      
      if (!mc->mangled_name) {
          char msg[256];
          snprintf(msg, 256, "Method '%s' resolution failed.", mc->method_name);
          codegen_error(ctx, (ASTNode*)mc, msg);
      }

      LLVMValueRef func = LLVMGetNamedFunction(ctx->module, mc->mangled_name);
      
      if (!func && mc->owner_class) {
           char fallback[256];
           snprintf(fallback, sizeof(fallback), "%s_%s", mc->owner_class, mc->method_name);
           func = LLVMGetNamedFunction(ctx->module, fallback);
      }

      if (!func) {
          char msg[256];
          snprintf(msg, 256, "Method '%s' not found.", mc->mangled_name);
          codegen_error(ctx, (ASTNode*)mc, msg);
      }
      
      int arg_count = 0;
      ASTNode *arg = mc->args;
      while(arg) { arg_count++; arg = arg->next; }
      
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * (arg_count + 1));
      args[0] = obj_ptr; 
      
      arg = mc->args;
      for(int i=0; i<arg_count; i++) {
          args[i+1] = codegen_expr(ctx, arg);
          arg = arg->next;
      }
      
      LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
      LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count + 1, "");
      free(args);
      return ret;
}

LLVMValueRef gen_var_ref(CodegenCtx *ctx, VarRefNode *node) {
      const char *name = node->name;
      Symbol *sym = find_symbol(ctx, name);
      if (sym) {
          if (sym->is_direct_value) return sym->value;

          if (sym->vtype.array_size > 0 && sym->vtype.ptr_depth == 0) {
              LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
              return LLVMBuildGEP2(ctx->builder, get_llvm_type(ctx, sym->vtype), sym->value, indices, 2, "arr_decay");
          }
          return LLVMBuildLoad2(ctx->builder, sym->type, sym->value, sym->name);
      }
      
      Symbol *this_sym = find_symbol(ctx, "this");
      if (this_sym && this_sym->vtype.class_name) {
           ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
           if (ci) {
               LLVMTypeRef mem_type;
               VarType mvt;
               int idx = get_member_index(ci, name, &mem_type, &mvt);
               
               if (idx != -1) {
                   LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder, this_sym->type, this_sym->value, "this_ptr");
                   LLVMValueRef mem_ptr;

                   if (ci->is_union) {
                       mem_ptr = LLVMBuildBitCast(ctx->builder, this_val, LLVMPointerType(mem_type, 0), "implicit_union_ptr");
                   } else {
                       LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                       mem_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, this_val, indices, 2, "implicit_mem_ptr");
                   }
                   
                   if (mvt.array_size > 0 && mvt.ptr_depth == 0) {
                       LLVMValueRef arr_indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
                       return LLVMBuildGEP2(ctx->builder, mem_type, mem_ptr, arr_indices, 2, "mem_arr_decay");
                   }

                   return LLVMBuildLoad2(ctx->builder, mem_type, mem_ptr, name);
               }
           }
      }

      char msg[256];
      snprintf(msg, sizeof(msg), "Undefined variable '%s'", name);
      codegen_error(ctx, (ASTNode*)node, msg);
      return NULL;
}

LLVMValueRef gen_cast(CodegenCtx *ctx, CastNode *cn) {
      LLVMValueRef val = codegen_expr(ctx, cn->operand);
      VarType from = codegen_calc_type(ctx, cn->operand);
      VarType to = cn->var_type;
      LLVMTypeRef to_type = get_llvm_type(ctx, to);
      
      int from_is_float = (from.base == TYPE_FLOAT || from.base == TYPE_DOUBLE || from.base == TYPE_LONG_DOUBLE);
      int to_is_float = (to.base == TYPE_FLOAT || to.base == TYPE_DOUBLE || to.base == TYPE_LONG_DOUBLE);
      int from_is_int = (from.base == TYPE_INT || from.base == TYPE_SHORT || from.base == TYPE_LONG || from.base == TYPE_LONG_LONG || from.base == TYPE_CHAR || from.base == TYPE_BOOL);
      int to_is_int = (to.base == TYPE_INT || to.base == TYPE_SHORT || to.base == TYPE_LONG || to.base == TYPE_LONG_LONG || to.base == TYPE_CHAR || to.base == TYPE_BOOL);
      int from_is_bool = (from.base == TYPE_BOOL);
      int to_is_bool = (to.base == TYPE_BOOL);

      if (from_is_int && to_is_float) {
           if (from.is_unsigned || from_is_bool) return LLVMBuildUIToFP(ctx->builder, val, to_type, "cast_ui_fp");
           return LLVMBuildSIToFP(ctx->builder, val, to_type, "cast_si_fp");
      }
      
      if (from_is_float && to_is_int) {
           if (to_is_bool) {
               return LLVMBuildFCmp(ctx->builder, LLVMRealONE, val, LLVMConstNull(LLVMTypeOf(val)), "cast_bool");
           }
           if (to.is_unsigned) return LLVMBuildFPToUI(ctx->builder, val, to_type, "cast_fp_ui");
           return LLVMBuildFPToSI(ctx->builder, val, to_type, "cast_fp_si");
      }
      
      if (from_is_int && to_is_int) {
           if (to_is_bool) {
               return LLVMBuildICmp(ctx->builder, LLVMIntNE, val, LLVMConstNull(LLVMTypeOf(val)), "cast_bool");
           }

           unsigned from_width = LLVMGetIntTypeWidth(LLVMTypeOf(val));
           unsigned to_width = LLVMGetIntTypeWidth(to_type);
           
           if (from_width < to_width) {
               if (from.is_unsigned || from_is_bool) return LLVMBuildZExt(ctx->builder, val, to_type, "cast_zext");
               return LLVMBuildSExt(ctx->builder, val, to_type, "cast_sext");
           } else if (from_width > to_width) {
               return LLVMBuildTrunc(ctx->builder, val, to_type, "cast_trunc");
           }
           return val;
      }
      
      if (from_is_float && to_is_float) {
           return LLVMBuildFPCast(ctx->builder, val, to_type, "cast_fp");
      }

      if (from.ptr_depth > 0 && to.ptr_depth > 0) {
          return LLVMBuildBitCast(ctx->builder, val, to_type, "cast_ptr");
      }
      
      if (from.ptr_depth > 0 && to_is_int) {
           if (to_is_bool) {
               return LLVMBuildICmp(ctx->builder, LLVMIntNE, val, LLVMConstPointerNull(LLVMTypeOf(val)), "cast_ptr_bool");
           }
           return LLVMBuildPtrToInt(ctx->builder, val, to_type, "cast_ptr_int");
      }

      if (from_is_int && to.ptr_depth > 0) {
           return LLVMBuildIntToPtr(ctx->builder, val, to_type, "cast_int_ptr");
      }

      return LLVMBuildBitCast(ctx->builder, val, to_type, "cast_generic");
}
