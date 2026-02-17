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
            LLVMValueRef val = codegen_expr(ctx, curr); 

            // Casting logic for function pointers (mirroring named function logic)
            if (i < var_sym->vtype.fp_param_count) {
                 VarType expected = var_sym->vtype.fp_param_types[i];
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
        
        // RECONSTRUCT FUNCTION TYPE FOR OPAQUE POINTER COMPATIBILITY
        VarType vt = var_sym->vtype;
        LLVMTypeRef ret_t = get_llvm_type(ctx, *vt.fp_ret_type);
        LLVMTypeRef *param_types = NULL;
        if (vt.fp_param_count > 0) {
            param_types = malloc(sizeof(LLVMTypeRef) * vt.fp_param_count);
            for(int i=0; i<vt.fp_param_count; i++) {
                param_types[i] = get_llvm_type(ctx, vt.fp_param_types[i]);
            }
        }
        LLVMTypeRef func_sig = LLVMFunctionType(ret_t, param_types, vt.fp_param_count, vt.fp_is_varargs);
        if (param_types) free(param_types);

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
