#include "codegen.h"
#include "../diagnostic/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward decl
LLVMValueRef generate_enum_to_string_func(CodegenCtx *ctx, EnumInfo *ei);

static LLVMTypeRef get_lvalue_struct_type(CodegenCtx *ctx, ASTNode *node) {
    if (!node) return NULL;

    if (node->type == NODE_VAR_REF) {
        Symbol *sym = find_symbol(ctx, ((VarRefNode*)node)->name);
        if (sym) return sym->type; // Returns the allocated type (e.g. [2 x [2 x i32]])

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

        // Only peel explicit Arrays. 
        // We do NOT inspect Pointers because they might be Opaque, crashing LLVMGetElementType.
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

// Calculate the Alkyl VarType of an expression (for type checking/inference in codegen)
VarType codegen_calc_type(CodegenCtx *ctx, ASTNode *node) {
    VarType vt = {TYPE_UNKNOWN, 0, NULL, 0, 0};
    if (!node) return vt;
    
    if (node->type == NODE_CAST) {
        return ((CastNode*)node)->var_type;
    }

    if (node->type == NODE_LITERAL) return ((LiteralNode*)node)->var_type;
    
    // Add logic for ARRAY_LIT type calculation
    if (node->type == NODE_ARRAY_LIT) {
        ArrayLitNode *an = (ArrayLitNode*)node;
        VarType t = {TYPE_INT, 0, NULL, 0, 0};
        if (an->elements) t = codegen_calc_type(ctx, an->elements);
        
        // Count elements
        int count = 0; ASTNode *el = an->elements; while(el){count++; el=el->next;}
        
        // Decay logic matching semantic analysis
        if (t.array_size > 0) {
            t.array_size = 0;
            t.ptr_depth++;
        }
        
        t.array_size = count;
        return t;
    }

    if (node->type == NODE_VAR_REF) {
        Symbol *s = find_symbol(ctx, ((VarRefNode*)node)->name);
        if (s) return s->vtype;
        
        // Check Enum Type
        EnumInfo *ei = find_enum(ctx, ((VarRefNode*)node)->name);
        if (ei) return (VarType){TYPE_INT, 0, NULL, 0, 0}; 

        // Implicit 'this' member type lookup
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
    }
    
    if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *aa = (ArrayAccessNode*)node;
        
        // Check Enum lookup
        if (aa->target->type == NODE_VAR_REF) {
            EnumInfo *ei = find_enum(ctx, ((VarRefNode*)aa->target)->name);
            if (ei) return (VarType){TYPE_STRING, 0, NULL, 0, 0};
        }

        VarType t = codegen_calc_type(ctx, aa->target);
        
        // PRIORITY FIX: Array vs Pointer precedence for Array Access
        // If it was an array (array_size > 0), access peels the array.
        // If it was a pointer (ptr_depth > 0), access dereferences.
        if (t.array_size > 0) {
            t.array_size = 0; 
        } else if (t.ptr_depth > 0) {
            t.ptr_depth--;
        }
        
        return t;
    }

    if (node->type == NODE_TRAIT_ACCESS) {
        TraitAccessNode *ta = (TraitAccessNode*)node;
        VarType t = codegen_calc_type(ctx, ta->object);
        if (t.base == TYPE_CLASS) {
            t.class_name = strdup(ta->trait_name);
            if (t.ptr_depth == 0) t.ptr_depth = 1;
            return t;
        }
    }

    if (node->type == NODE_MEMBER_ACCESS) {
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
    }
    
    if (node->type == NODE_CALL) {
        CallNode *call = (CallNode*)node;
        const char *name = call->mangled_name ? call->mangled_name : call->name;
        
        // FIX: Handle NULL name to prevent strcmp segfault
        if (!name) return vt;

        FuncSymbol *fs = find_func_symbol(ctx, name);
        if (fs) return fs->ret_type;
        
        ClassInfo *ci = find_class(ctx, call->name); 
        if (ci) {
            vt.base = TYPE_CLASS;
            vt.class_name = strdup(call->name);
            vt.ptr_depth = 1; 
            return vt;
        }

        if (strcmp(name, "input") == 0) { vt.base = TYPE_STRING; return vt; }
    }

    if (node->type == NODE_METHOD_CALL) {
        MethodCallNode *mc = (MethodCallNode*)node;
        if (mc->mangled_name) {
             FuncSymbol *fs = find_func_symbol(ctx, mc->mangled_name);
             if (fs) return fs->ret_type;
        }
    }
    
    if (node->type == NODE_TYPEOF) {
        return (VarType){TYPE_STRING, 0, NULL, 0, 0};
    }
    
    if (node->type == NODE_HAS_METHOD || node->type == NODE_HAS_ATTRIBUTE) {
        VarType ret = {TYPE_STRING, 0, NULL, 0, 0};
        ret.ptr_depth = 1; // string* return ret;
    }
    
    if (node->type == NODE_UNARY_OP) {
        UnaryOpNode *u = (UnaryOpNode*)node;
        VarType t = codegen_calc_type(ctx, u->operand);
        if (u->op == TOKEN_AND) { t.ptr_depth++; return t; }
        if (u->op == TOKEN_STAR) { if (t.ptr_depth > 0) t.ptr_depth--; return t; }
        if (u->op == TOKEN_NOT) { return (VarType){TYPE_BOOL, 0, NULL, 0, 0}; }
        return t; 
    }

    // FIX: ADDED NODE_BINARY_OP TYPE CALCULATION TO SUPPORT CASTING RESULTS
    if (node->type == NODE_BINARY_OP) {
        BinaryOpNode *op = (BinaryOpNode*)node;
        
        // 1. Relational/Logical Ops -> Bool
        if (op->op == TOKEN_EQ || op->op == TOKEN_NEQ ||
            op->op == TOKEN_LT || op->op == TOKEN_GT ||
            op->op == TOKEN_LTE || op->op == TOKEN_GTE) {
            return (VarType){TYPE_BOOL, 0, NULL, 0, 0};
        }

        VarType l = codegen_calc_type(ctx, op->left);
        VarType r = codegen_calc_type(ctx, op->right);
        
        // 2. String Concatenation
        if (op->op == TOKEN_PLUS && (l.base == TYPE_STRING || r.base == TYPE_STRING)) {
             return (VarType){TYPE_STRING, 0, NULL, 0, 0};
        }
        
        // 3. Float/Double Promotion
        if (l.base == TYPE_LONG_DOUBLE || r.base == TYPE_LONG_DOUBLE) return (VarType){TYPE_LONG_DOUBLE, 0, NULL, 0, 0};
        if (l.base == TYPE_DOUBLE || r.base == TYPE_DOUBLE) return (VarType){TYPE_DOUBLE, 0, NULL, 0, 0};
        if (l.base == TYPE_FLOAT || r.base == TYPE_FLOAT) return (VarType){TYPE_FLOAT, 0, NULL, 0, 0};
        
        // 4. Int Promotion
        if (l.base == TYPE_LONG_LONG || r.base == TYPE_LONG_LONG) return (VarType){TYPE_LONG_LONG, 0, NULL, 0, 0};
        if (l.base == TYPE_LONG || r.base == TYPE_LONG) return (VarType){TYPE_LONG, 0, NULL, 0, 0};
        
        // Default to left type (likely int)
        return l;
    }
    
    return vt;
}

// Generate the memory address (l-value) of an expression
LLVMValueRef codegen_addr(CodegenCtx *ctx, ASTNode *node) {
     if (node->type == NODE_VAR_REF) {
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
     }
     
     if (node->type == NODE_ARRAY_ACCESS) {
         ArrayAccessNode *aa = (ArrayAccessNode*)node;
         if (aa->target->type == NODE_VAR_REF) {
              if (find_enum(ctx, ((VarRefNode*)aa->target)->name)) return NULL;
         }

         // Get the address of the target
         LLVMValueRef target = codegen_addr(ctx, aa->target);
         
         // If target is NULL (e.g. r-value array), we can't get an address
         if (!target) return NULL;

         LLVMValueRef index = codegen_expr(ctx, aa->index);
         
         // ROBUST ARRAY LOGIC:
         // Use get_lvalue_struct_type to inspect the structural type of the target address.
         // This handles Multidimensional Arrays (explicit types).
         
         LLVMTypeRef target_struct_type = get_lvalue_struct_type(ctx, aa->target);
         
         if (target_struct_type && LLVMGetTypeKind(target_struct_type) == LLVMArrayTypeKind) {
             // Case 1: Target points to an explicit Array (e.g. [10 x i32]* or [2 x [2 x i32]]*)
             // We need GEP(0, index) to peel one layer
             LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), index };
             return LLVMBuildGEP2(ctx->builder, target_struct_type, target, indices, 2, "arr_idx");
         } 
         
         // Case 2: Pointers (including opaque pointers)
         // Fallback to VarType info which is safe against Opaque Pointers.
         
         VarType t = codegen_calc_type(ctx, aa->target);
         LLVMTypeRef llvm_t = get_llvm_type(ctx, t);
         
         if (t.ptr_depth > 0) {
             LLVMValueRef base = LLVMBuildLoad2(ctx->builder, llvm_t, target, "ptr_base");
             t.ptr_depth--;
             LLVMTypeRef inner_type = get_llvm_type(ctx, t);
             return LLVMBuildGEP2(ctx->builder, inner_type, base, &index, 1, "ptr_idx");
         }
     }

     if (node->type == NODE_MEMBER_ACCESS) {
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
                         // UNION: Bitcast pointer to member type pointer
                         LLVMValueRef base = obj;
                         if (obj_t.ptr_depth > 0) {
                             if (obj) base = LLVMBuildLoad2(ctx->builder, LLVMPointerType(ci->struct_type, 0), obj, "obj_ptr");
                             else base = codegen_expr(ctx, ma->object);
                         } else {
                            if (!obj) {
                                char msg[256];
                                snprintf(msg, 256, "Cannot access union member '%s' of r-value", ma->member_name);
                                codegen_error(ctx, node, msg);
                            }
                         }
                         return LLVMBuildBitCast(ctx->builder, base, LLVMPointerType(member_type, 0), "union_mem_ptr");
                     } else {
                        // STRUCT: Use GEP
                        if (obj_t.ptr_depth > 0) {
                            LLVMValueRef base;
                            if (obj) {
                                base = LLVMBuildLoad2(ctx->builder, LLVMPointerType(ci->struct_type, 0), obj, "obj_ptr");
                            } else {
                                base = codegen_expr(ctx, ma->object);
                            }
                            LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                            return LLVMBuildGEP2(ctx->builder, ci->struct_type, base, indices, 2, "mem_ptr");
                        } else {
                            if (!obj) {
                                char msg[256];
                                snprintf(msg, 256, "Cannot access member '%s' of r-value struct", ma->member_name);
                                codegen_error(ctx, node, msg);
                            }
                            LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                            return LLVMBuildGEP2(ctx->builder, ci->struct_type, obj, indices, 2, "mem_ptr");
                        }
                     }
                 }
             }
         }
     }

     if (node->type == NODE_UNARY_OP) {
         UnaryOpNode *u = (UnaryOpNode*)node;
         if (u->op == TOKEN_STAR) {
             return codegen_expr(ctx, u->operand);
         }
     }
     
     return NULL;
}

LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node) {
  if (!node) return LLVMConstInt(LLVMInt32Type(), 0, 0);

  debug_flow("Enter: Node type for codegen expr: %s", node_type_to_string(node->type));

  if (node->type == NODE_CALL) {
    CallNode *c = (CallNode*)node;
    
    // FIX: Guard against NULL name in calls
    if (!c->name) {
        codegen_error(ctx, node, "Function call with no name");
        return LLVMConstInt(LLVMInt32Type(), 0, 0);
    }

    ClassInfo *ci = find_class(ctx, c->name);
    if (ci) {
        LLVMValueRef size = LLVMSizeOf(ci->struct_type);
        LLVMValueRef mem = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "new_obj");
        LLVMValueRef obj = LLVMBuildBitCast(ctx->builder, mem, LLVMPointerType(ci->struct_type, 0), "obj_cast");
        
        ASTNode *arg = c->args;
        ClassMember *m = ci->members;
        
        if (ci->is_union) {
            // UNION CONSTRUCTOR Logic
            if (arg) {
                 VarType r_vt = codegen_calc_type(ctx, arg);
                 LLVMValueRef arg_val = codegen_expr(ctx, arg);
                 
                 // Find matching member
                 ClassMember *match = NULL;
                 ClassMember *iter = ci->members;
                 while(iter) {
                      // Match logic similar to stmt.c
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
                      // Store logic
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
                 // Ignore subsequent args for union or handle them? Usually 1 arg.
            }
        } else {
             // Constructor logic for Struct
             while (arg && m) {
                LLVMValueRef arg_val = codegen_expr(ctx, arg);
                LLVMValueRef mem_ptr;
                
                // Struct: GEP to member index
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

    const char *target_name = c->mangled_name ? c->mangled_name : c->name;
    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, target_name);
    
    if (!func) {
        func = LLVMGetNamedFunction(ctx->module, c->name);
        if (!func) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Undefined function '%s'.", c->name);
            codegen_error(ctx, node, msg);
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
             
             // Quick implicit cast injection if semantic analysis didn't cover generic scenarios
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
  else if (node->type == NODE_METHOD_CALL) {
      MethodCallNode *mc = (MethodCallNode*)node;

      if (mc->is_static) {
          if (!mc->mangled_name) codegen_error(ctx, node, "Static call missing mangled name");
          
          LLVMValueRef func = LLVMGetNamedFunction(ctx->module, mc->mangled_name);
          if (!func) codegen_error(ctx, node, "Static function not found in module");
          
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
      
      if (!obj_ptr) codegen_error(ctx, node, "Invalid object for method call");
      
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
          snprintf(msg, 256, "Method '%s' resolution failed (mangled name is null).", mc->method_name);
          codegen_error(ctx, node, msg);
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
          codegen_error(ctx, node, msg);
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
  debug_flow("Enter: Node type for codegen expr: %s", node_type_to_string(node->type));

      return ret;
  }
  else if (node->type == NODE_TRAIT_ACCESS) {
      TraitAccessNode *ta = (TraitAccessNode*)node;
      
      LLVMValueRef obj_ptr = NULL;
      VarType obj_t = codegen_calc_type(ctx, ta->object);
      
      if (obj_t.ptr_depth > 0) obj_ptr = codegen_expr(ctx, ta->object);
      else obj_ptr = codegen_addr(ctx, ta->object);
      
      if (!obj_ptr) codegen_error(ctx, node, "Cannot access trait of null/invalid object");
      if (!obj_t.class_name) codegen_error(ctx, node, "Object type has no class name");
      
      ClassInfo *ci = find_class(ctx, obj_t.class_name);
      if (!ci) codegen_error(ctx, node, "Object is not a class");

      int offset = get_trait_offset(ctx, ci, ta->trait_name);
      if (offset == -1) {
          char msg[256]; snprintf(msg, 256, "Class '%s' does not have trait '%s'", ci->name, ta->trait_name);
          codegen_error(ctx, node, msg);
      }
      
      ClassInfo *trait = find_class(ctx, ta->trait_name);
      LLVMTypeRef trait_type = trait ? trait->struct_type : LLVMStructCreateNamed(LLVMGetGlobalContext(), ta->trait_name);
      
      LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), offset, 0) };
      LLVMValueRef trait_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, obj_ptr, indices, 2, "trait_ptr");
      
      return LLVMBuildBitCast(ctx->builder, trait_ptr, LLVMPointerType(trait_type, 0), "trait_cast");
  }
  else if (node->type == NODE_ARRAY_LIT) {
        // Implement Array Literals as expression (R-Values)
        // Allocates a temporary array and returns a decayed pointer to it.
        // This is crucial for nested arrays (e.g., [[1,2]]) where inner arrays must be pointers.
        
        ArrayLitNode *an = (ArrayLitNode*)node;
        
        // Count elements
        int count = 0; ASTNode *el = an->elements; while(el){count++; el=el->next;}
        
        // Determine element type
        VarType et = {TYPE_INT, 0, NULL, 0, 0};
        if (an->elements) et = codegen_calc_type(ctx, an->elements);
        
        // If element is an array (nested), it should decay to pointer
        if (et.array_size > 0) {
            et.array_size = 0;
            et.ptr_depth++;
        }
        
        LLVMTypeRef llvm_et = get_llvm_type(ctx, et);
        LLVMTypeRef arr_type = LLVMArrayType(llvm_et, count);
        
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, arr_type, "arr_lit_tmp");
        
        int idx = 0;
        el = an->elements;
        while(el) {
            LLVMValueRef val = codegen_expr(ctx, el);
            
            // Basic Auto-cast for literals
            if (LLVMGetTypeKind(llvm_et) == LLVMDoubleTypeKind && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                val = LLVMBuildSIToFP(ctx->builder, val, llvm_et, "cast");
            } else if (LLVMGetTypeKind(llvm_et) == LLVMIntegerTypeKind && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                if (LLVMGetIntTypeWidth(llvm_et) > LLVMGetIntTypeWidth(LLVMTypeOf(val))) {
                    val = LLVMBuildSExt(ctx->builder, val, llvm_et, "cast");
                }
            }

            LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), idx, 0) };
            LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, arr_type, alloca, indices, 2, "elem");
            LLVMBuildStore(ctx->builder, val, ptr);
            
            el = el->next;
            idx++;
        }
        
        // Return pointer to start (decay)
        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
        return LLVMBuildGEP2(ctx->builder, arr_type, alloca, indices, 2, "arr_decay");
  }
  else if (node->type == NODE_LITERAL) {
    LiteralNode *l = (LiteralNode*)node;
    
    int is_string = (l->var_type.base == TYPE_STRING) || 
                    (l->var_type.base == TYPE_CHAR && l->var_type.ptr_depth > 0);

    if (is_string) {
        if (l->val.str_val) {
          char *fmt = format_string(l->val.str_val);
          LLVMValueRef gstr = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "str_lit");
          free(fmt);
          return gstr;
        } else {
            return LLVMConstPointerNull(LLVMPointerType(LLVMInt8Type(), 0));
        }
    }

    if (l->var_type.base == TYPE_DOUBLE) return LLVMConstReal(LLVMDoubleType(), l->val.double_val);
    if (l->var_type.base == TYPE_LONG_DOUBLE) return LLVMConstReal(LLVMFP128Type(), l->val.double_val);
    if (l->var_type.base == TYPE_FLOAT) return LLVMConstReal(LLVMFloatType(), l->val.double_val);
    if (l->var_type.base == TYPE_BOOL) return LLVMConstInt(LLVMInt1Type(), l->val.long_val, 0);
    if (l->var_type.base == TYPE_CHAR) return LLVMConstInt(LLVMInt8Type(), l->val.long_val, 0); 
    
    return LLVMConstInt(get_llvm_type(ctx, l->var_type), l->val.long_val, 0);
  }
  else if (node->type == NODE_BINARY_OP) {
      BinaryOpNode *op = (BinaryOpNode*)node;
      LLVMValueRef l = codegen_expr(ctx, op->left);
      LLVMValueRef r = codegen_expr(ctx, op->right);

      VarType lt = codegen_calc_type(ctx, op->left);
      VarType rt = codegen_calc_type(ctx, op->right);
      
      // Alkyl String Concatenation: +
      if (lt.base == TYPE_STRING && rt.base == TYPE_STRING && op->op == TOKEN_PLUS) {
             LLVMValueRef len1 = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strlen_func), ctx->strlen_func, &l, 1, "len1");
             LLVMValueRef len2 = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strlen_func), ctx->strlen_func, &r, 1, "len2");
             LLVMValueRef sum = LLVMBuildAdd(ctx->builder, len1, len2, "len_sum");
             LLVMValueRef size = LLVMBuildAdd(ctx->builder, sum, LLVMConstInt(LLVMInt64Type(), 1, 0), "alloc_size");
             
             LLVMValueRef mem = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "new_str");
             
             LLVMValueRef args_cpy[] = { mem, l };
             LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args_cpy, 2, "");
             
             LLVMValueRef idxs[] = { len1 };
             LLVMValueRef ptr2 = LLVMBuildGEP2(ctx->builder, LLVMInt8Type(), mem, idxs, 1, "ptr2");
             LLVMValueRef args_cpy2[] = { ptr2, r };
             LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args_cpy2, 2, "");
             
             return mem;
      }
      
      // Alkyl String Comparison: ==, !=
      if (lt.base == TYPE_STRING && rt.base == TYPE_STRING && (op->op == TOKEN_EQ || op->op == TOKEN_NEQ)) {
             LLVMValueRef args[] = {l, r};
             LLVMValueRef cmp_res = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcmp_func), ctx->strcmp_func, args, 2, "cmp_res");
             
             if (op->op == TOKEN_EQ) 
                 return LLVMBuildICmp(ctx->builder, LLVMIntEQ, cmp_res, LLVMConstInt(LLVMInt32Type(), 0, 0), "str_eq");
             else
                 return LLVMBuildICmp(ctx->builder, LLVMIntNE, cmp_res, LLVMConstInt(LLVMInt32Type(), 0, 0), "str_neq");
      }

      // Robust Auto-Cast for LLVM
      if (LLVMTypeOf(l) != LLVMTypeOf(r)) {
          LLVMTypeKind lk = LLVMGetTypeKind(LLVMTypeOf(l));
          LLVMTypeKind rk = LLVMGetTypeKind(LLVMTypeOf(r));
          if (lk == LLVMDoubleTypeKind && rk == LLVMIntegerTypeKind) {
              r = rt.is_unsigned ? LLVMBuildUIToFP(ctx->builder, r, LLVMTypeOf(l), "cast") : LLVMBuildSIToFP(ctx->builder, r, LLVMTypeOf(l), "cast");
          } else if (lk == LLVMIntegerTypeKind && rk == LLVMDoubleTypeKind) {
              l = lt.is_unsigned ? LLVMBuildUIToFP(ctx->builder, l, LLVMTypeOf(r), "cast") : LLVMBuildSIToFP(ctx->builder, l, LLVMTypeOf(r), "cast");
          } else if (lk == LLVMFloatTypeKind && rk == LLVMIntegerTypeKind) {
              r = rt.is_unsigned ? LLVMBuildUIToFP(ctx->builder, r, LLVMTypeOf(l), "cast") : LLVMBuildSIToFP(ctx->builder, r, LLVMTypeOf(l), "cast");
          } else if (lk == LLVMIntegerTypeKind && rk == LLVMFloatTypeKind) {
              l = lt.is_unsigned ? LLVMBuildUIToFP(ctx->builder, l, LLVMTypeOf(r), "cast") : LLVMBuildSIToFP(ctx->builder, l, LLVMTypeOf(r), "cast");
          } else if (lk == LLVMIntegerTypeKind && rk == LLVMIntegerTypeKind) {
              unsigned lw = LLVMGetIntTypeWidth(LLVMTypeOf(l));
              unsigned rw = LLVMGetIntTypeWidth(LLVMTypeOf(r));
              if (lw > rw) r = rt.is_unsigned ? LLVMBuildZExt(ctx->builder, r, LLVMTypeOf(l), "cast") : LLVMBuildSExt(ctx->builder, r, LLVMTypeOf(l), "cast");
              else if (rw > lw) l = lt.is_unsigned ? LLVMBuildZExt(ctx->builder, l, LLVMTypeOf(r), "cast") : LLVMBuildSExt(ctx->builder, l, LLVMTypeOf(r), "cast");
          } else if (lk == LLVMDoubleTypeKind && rk == LLVMFloatTypeKind) {
              r = LLVMBuildFPExt(ctx->builder, r, LLVMTypeOf(l), "cast");
          } else if (lk == LLVMFloatTypeKind && rk == LLVMDoubleTypeKind) {
              l = LLVMBuildFPExt(ctx->builder, l, LLVMTypeOf(r), "cast");
          } else if (lk == LLVMFP128TypeKind && (rk == LLVMDoubleTypeKind || rk == LLVMFloatTypeKind)) {
              r = LLVMBuildFPExt(ctx->builder, r, LLVMTypeOf(l), "cast");
          } else if ((lk == LLVMDoubleTypeKind || lk == LLVMFloatTypeKind) && rk == LLVMFP128TypeKind) {
              l = LLVMBuildFPExt(ctx->builder, l, LLVMTypeOf(r), "cast");
          } else {
              r = LLVMBuildBitCast(ctx->builder, r, LLVMTypeOf(l), "cast");
          }
      }

      int is_float = (lt.base == TYPE_FLOAT || lt.base == TYPE_DOUBLE || lt.base == TYPE_LONG_DOUBLE || 
                      rt.base == TYPE_FLOAT || rt.base == TYPE_DOUBLE || rt.base == TYPE_LONG_DOUBLE);
      int is_unsigned = (lt.is_unsigned || rt.is_unsigned);

      // Basic Math Ops
      if (op->op == TOKEN_PLUS) {
          if (is_float) return LLVMBuildFAdd(ctx->builder, l, r, "fadd");
          return LLVMBuildAdd(ctx->builder, l, r, "add");
      }
      if (op->op == TOKEN_MINUS) {
          if (is_float) return LLVMBuildFSub(ctx->builder, l, r, "fsub");
          return LLVMBuildSub(ctx->builder, l, r, "sub");
      }
      if (op->op == TOKEN_STAR) {
          if (is_float) return LLVMBuildFMul(ctx->builder, l, r, "fmul");
          return LLVMBuildMul(ctx->builder, l, r, "mul");
      }
      if (op->op == TOKEN_SLASH) {
          if (is_float) return LLVMBuildFDiv(ctx->builder, l, r, "fdiv");
          if (is_unsigned) return LLVMBuildUDiv(ctx->builder, l, r, "udiv");
          return LLVMBuildSDiv(ctx->builder, l, r, "sdiv");
      }
      if (op->op == TOKEN_MOD) {
          if (is_float) return LLVMBuildFRem(ctx->builder, l, r, "frem");
          if (is_unsigned) return LLVMBuildURem(ctx->builder, l, r, "urem");
          return LLVMBuildSRem(ctx->builder, l, r, "srem");
      }

      // Relational Ops
      if (op->op == TOKEN_LT) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOLT, l, r, "flt");
          if (is_unsigned) return LLVMBuildICmp(ctx->builder, LLVMIntULT, l, r, "ult");
          return LLVMBuildICmp(ctx->builder, LLVMIntSLT, l, r, "slt");
      }
      if (op->op == TOKEN_GT) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOGT, l, r, "fgt");
          if (is_unsigned) return LLVMBuildICmp(ctx->builder, LLVMIntUGT, l, r, "ugt");
          return LLVMBuildICmp(ctx->builder, LLVMIntSGT, l, r, "sgt");
      }
      if (op->op == TOKEN_LTE) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOLE, l, r, "flte");
          if (is_unsigned) return LLVMBuildICmp(ctx->builder, LLVMIntULE, l, r, "ulte");
          return LLVMBuildICmp(ctx->builder, LLVMIntSLE, l, r, "slte");
      }
      if (op->op == TOKEN_GTE) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOGE, l, r, "fgte");
          if (is_unsigned) return LLVMBuildICmp(ctx->builder, LLVMIntUGE, l, r, "ugte");
          return LLVMBuildICmp(ctx->builder, LLVMIntSGE, l, r, "sgte");
      }
      if (op->op == TOKEN_EQ) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, l, r, "feq");
          return LLVMBuildICmp(ctx->builder, LLVMIntEQ, l, r, "eq");
      }
      if (op->op == TOKEN_NEQ) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealONE, l, r, "fneq");
          return LLVMBuildICmp(ctx->builder, LLVMIntNE, l, r, "neq");
      }

      return LLVMConstInt(LLVMInt32Type(), 0, 0);
  }
  else if (node->type == NODE_UNARY_OP) {
      UnaryOpNode *u = (UnaryOpNode*)node;
      
      if (u->op == TOKEN_AND) {
          return codegen_addr(ctx, u->operand);
      }
      
      LLVMValueRef operand = codegen_expr(ctx, u->operand);
      VarType t = codegen_calc_type(ctx, u->operand);
      
      if (u->op == TOKEN_MINUS) {
          if (t.base == TYPE_FLOAT || t.base == TYPE_DOUBLE || t.base == TYPE_LONG_DOUBLE) 
              return LLVMBuildFNeg(ctx->builder, operand, "neg");
          else 
              return LLVMBuildNeg(ctx->builder, operand, "neg");
      }
      if (u->op == TOKEN_NOT) {
          if (t.base == TYPE_FLOAT || t.base == TYPE_DOUBLE || t.base == TYPE_LONG_DOUBLE)
              return LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, operand, LLVMConstNull(LLVMTypeOf(operand)), "not");
          else
              return LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand, LLVMConstNull(LLVMTypeOf(operand)), "not");
      }
      if (u->op == TOKEN_BIT_NOT) {
          return LLVMBuildNot(ctx->builder, operand, "bit_not");
      }
      if (u->op == TOKEN_STAR) {
          VarType vt = codegen_calc_type(ctx, u->operand);
          if (vt.ptr_depth > 0) vt.ptr_depth--;
          LLVMTypeRef load_type = get_llvm_type(ctx, vt);
          return LLVMBuildLoad2(ctx->builder, load_type, operand, "deref");
      }
  }
  else if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *aa = (ArrayAccessNode*)node;
        VarType target_t = codegen_calc_type(ctx, aa->target);

        if (aa->target->type == NODE_VAR_REF) {
             const char *name = ((VarRefNode*)aa->target)->name;
             EnumInfo *ei = find_enum(ctx, name);
             if (ei) {
                 LLVMValueRef idx = codegen_expr(ctx, aa->index);
                 if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
                    idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt32Type(), "idx_cast");
                 } else {
                    idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt32Type(), "idx_cast");
                 }
                 
                 return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ei->to_string_func), ei->to_string_func, &idx, 1, "enum_str");
             }
        }
        
        if (target_t.base == TYPE_STRING) {
             LLVMValueRef target = codegen_expr(ctx, aa->target);
             LLVMValueRef index = codegen_expr(ctx, aa->index);
             LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, LLVMInt8Type(), target, &index, 1, "str_idx");
             return LLVMBuildLoad2(ctx->builder, LLVMInt8Type(), gep, "char_val");
        }
        
        // PRIORITY FIX:
        // Prioritize using codegen_addr logic (which inspects LLVM types via get_lvalue_struct_type) 
        // for correct Array Access.
        
        LLVMValueRef target_ptr = codegen_addr(ctx, node);
        if (target_ptr) {
            // Determine the structural type pointed to by target_ptr
            LLVMTypeRef struct_type = get_lvalue_struct_type(ctx, node);
            
            // If the RESULT of the access (what target_ptr points to) is an Array, we decay it.
            if (struct_type && LLVMGetTypeKind(struct_type) == LLVMArrayTypeKind) {
                 LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
                 return LLVMBuildGEP2(ctx->builder, struct_type, target_ptr, indices, 2, "arr_decay");
            } else if (struct_type) {
                 return LLVMBuildLoad2(ctx->builder, struct_type, target_ptr, "arr_val");
            } else {
                 // Fallback if struct_type check failed but we have a pointer
                 LLVMTypeRef elem_type = get_llvm_type(ctx, target_t); 
                 return LLVMBuildLoad2(ctx->builder, elem_type, target_ptr, "arr_val");
            }
        }
        
        // Fallback for R-Values (e.g. [1,2][0]) where codegen_addr returns NULL
        LLVMValueRef target = codegen_expr(ctx, aa->target); 
        LLVMValueRef index = codegen_expr(ctx, aa->index);
        LLVMTypeRef el_type = get_llvm_type(ctx, target_t); 
        
        if (target_t.array_size > 0) {
             // Array: GEP(0, idx)
             LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), index };
             LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, el_type, target, indices, 2, "arr_idx");
             return LLVMBuildLoad2(ctx->builder, LLVMGetElementType(el_type), gep, "val");
        } 
        else if (target_t.ptr_depth > 0) {
            // Pointer: Load base, then GEP(idx)
            el_type = LLVMGetElementType(el_type);
            // If target is already a pointer value, we use it as base
            LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, LLVMGetElementType(el_type), target, &index, 1, "ptr_idx");
            return LLVMBuildLoad2(ctx->builder, LLVMGetElementType(el_type), gep, "val");
        }
  }
  else if (node->type == NODE_MEMBER_ACCESS) {
      MemberAccessNode *ma = (MemberAccessNode*)node;
      
      if (ma->object->type == NODE_VAR_REF) {
           const char *obj_name = ((VarRefNode*)ma->object)->name;
           EnumInfo *ei = find_enum(ctx, obj_name);
           if (ei) {
               EnumEntryInfo *curr = ei->entries;
               while(curr) {
                   if (strcmp(curr->name, ma->member_name) == 0) {
                       return LLVMConstInt(LLVMInt32Type(), curr->value, 0);
                   }
                   curr = curr->next;
               }
               codegen_error(ctx, node, "Enum member not found in codegen");
           }
      }

      LLVMValueRef addr = codegen_addr(ctx, node);
      if (addr) {
          VarType vt = codegen_calc_type(ctx, node);
          LLVMTypeRef type = get_llvm_type(ctx, vt);

          // Use structural type check for robust decay logic if available
          LLVMTypeRef struct_type = get_lvalue_struct_type(ctx, node);
          if (struct_type && LLVMGetTypeKind(struct_type) == LLVMArrayTypeKind) {
               LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
               return LLVMBuildGEP2(ctx->builder, struct_type, addr, indices, 2, "mem_decay");
          }

          if (vt.array_size > 0 && vt.ptr_depth == 0) {
               // Fallback Decay
               LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
               return LLVMBuildGEP2(ctx->builder, get_llvm_type(ctx, vt), addr, indices, 2, "mem_decay");
          }

          return LLVMBuildLoad2(ctx->builder, type, addr, "mem_val");
      }
  }
  else if (node->type == NODE_INC_DEC) {
      IncDecNode *id = (IncDecNode*)node;
      
      LLVMValueRef addr = codegen_addr(ctx, id->target);
      if (!addr) {
          codegen_error(ctx, node, "Operand must be an l-value");
      }
      
      VarType vt = codegen_calc_type(ctx, id->target);
      LLVMTypeRef type = get_llvm_type(ctx, vt);
      LLVMValueRef old_val = LLVMBuildLoad2(ctx->builder, type, addr, "old_val");
      
      LLVMValueRef new_val = NULL;
      LLVMValueRef one = NULL;
      
      int is_float = (vt.base == TYPE_FLOAT || vt.base == TYPE_DOUBLE || vt.base == TYPE_LONG_DOUBLE);
      
      if (is_float) {
          one = LLVMConstReal(type, 1.0);
          if (id->op == TOKEN_INCREMENT) 
              new_val = LLVMBuildFAdd(ctx->builder, old_val, one, "new_val");
          else 
              new_val = LLVMBuildFSub(ctx->builder, old_val, one, "new_val");
      } else {
          one = LLVMConstInt(type, 1, 0);
          if (id->op == TOKEN_INCREMENT) 
              new_val = LLVMBuildAdd(ctx->builder, old_val, one, "new_val");
          else 
              new_val = LLVMBuildSub(ctx->builder, old_val, one, "new_val");
      }
      
      LLVMBuildStore(ctx->builder, new_val, addr);
      return id->is_prefix ? new_val : old_val;
  }
  else if (node->type == NODE_VAR_REF) {
      const char *name = ((VarRefNode*)node)->name;
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
      codegen_error(ctx, node, msg);
  }
  else if (node->type == NODE_TYPEOF) {
      UnaryOpNode *tn = (UnaryOpNode*)node;
      VarType t = codegen_calc_type(ctx, tn->operand);
      
      char buf[256];
      get_type_name(t, buf);
      
      return LLVMBuildGlobalStringPtr(ctx->builder, buf, "typeof_str");
  }
  else if (node->type == NODE_HAS_METHOD) {
      UnaryOpNode *u = (UnaryOpNode*)node;
      VarType t = codegen_calc_type(ctx, u->operand);
      if (t.class_name) {
          ClassInfo *ci = find_class(ctx, t.class_name);
          if (!ci) codegen_error(ctx, node, "Unknown class for hasmethod");
          
          int count = ci->method_count;
          LLVMTypeRef str_type = LLVMPointerType(LLVMInt8Type(), 0);
          LLVMTypeRef arr_type = LLVMArrayType(str_type, count + 1); 
          
          LLVMValueRef *vals = malloc(sizeof(LLVMValueRef) * (count + 1));
          for(int i=0; i<count; i++) {
              vals[i] = LLVMBuildGlobalStringPtr(ctx->builder, ci->method_names[i], "method_name");
          }
          vals[count] = LLVMConstPointerNull(str_type);

          LLVMValueRef const_arr = LLVMConstArray(str_type, vals, count + 1);
          LLVMValueRef global_arr = LLVMAddGlobal(ctx->module, arr_type, "method_list");
          LLVMSetInitializer(global_arr, const_arr);
          LLVMSetGlobalConstant(global_arr, 1);
          LLVMSetLinkage(global_arr, LLVMPrivateLinkage);
          
          free(vals);
          
          LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
          return LLVMBuildGEP2(ctx->builder, arr_type, global_arr, indices, 2, "method_list_ptr");
      }
      return LLVMConstPointerNull(LLVMPointerType(LLVMPointerType(LLVMInt8Type(), 0), 0));
  }
  else if (node->type == NODE_HAS_ATTRIBUTE) {
      UnaryOpNode *u = (UnaryOpNode*)node;
      VarType t = codegen_calc_type(ctx, u->operand);
      if (t.class_name) {
          ClassInfo *ci = find_class(ctx, t.class_name);
          if (!ci) codegen_error(ctx, node, "Unknown class for hasattribute");
          
          int count = 0;
          ClassMember *m = ci->members;
          while(m) { count++; m = m->next; }
          
          LLVMTypeRef str_type = LLVMPointerType(LLVMInt8Type(), 0);
          LLVMTypeRef arr_type = LLVMArrayType(str_type, count + 1);
          
          LLVMValueRef *vals = malloc(sizeof(LLVMValueRef) * (count + 1));
          m = ci->members;
          int i = 0;
          while(m) {
              vals[i++] = LLVMBuildGlobalStringPtr(ctx->builder, m->name, "attr_name");
              m = m->next;
          }
          vals[count] = LLVMConstPointerNull(str_type);

          LLVMValueRef const_arr = LLVMConstArray(str_type, vals, count + 1);
          LLVMValueRef global_arr = LLVMAddGlobal(ctx->module, arr_type, "attr_list");
          LLVMSetInitializer(global_arr, const_arr);
          LLVMSetGlobalConstant(global_arr, 1);
          LLVMSetLinkage(global_arr, LLVMPrivateLinkage);
          
          free(vals);
          
          LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
          return LLVMBuildGEP2(ctx->builder, arr_type, global_arr, indices, 2, "attr_list_ptr");
      }
      return LLVMConstPointerNull(LLVMPointerType(LLVMPointerType(LLVMInt8Type(), 0), 0));
  }
  else if (node->type == NODE_CAST) {
      CastNode *cn = (CastNode*)node;
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

      // Int/Bool -> Float
      if (from_is_int && to_is_float) {
           // Bools and Unsigned Ints use UIToFP
           if (from.is_unsigned || from_is_bool) return LLVMBuildUIToFP(ctx->builder, val, to_type, "cast_ui_fp");
           // Signed Ints use SIToFP
           return LLVMBuildSIToFP(ctx->builder, val, to_type, "cast_si_fp");
      }
      
      // Float -> Int/Bool
      if (from_is_float && to_is_int) {
           // Float -> Bool (Comparison against 0.0)
           if (to_is_bool) {
               return LLVMBuildFCmp(ctx->builder, LLVMRealONE, val, LLVMConstNull(LLVMTypeOf(val)), "cast_bool");
           }
           // Float -> Unsigned Int
           if (to.is_unsigned) return LLVMBuildFPToUI(ctx->builder, val, to_type, "cast_fp_ui");
           // Float -> Signed Int
           return LLVMBuildFPToSI(ctx->builder, val, to_type, "cast_fp_si");
      }
      
      // Int -> Int/Bool
      if (from_is_int && to_is_int) {
           // Int -> Bool (Comparison against 0)
           // Prevents truncation issues (e.g. 2 -> 0 in i1)
           if (to_is_bool) {
               return LLVMBuildICmp(ctx->builder, LLVMIntNE, val, LLVMConstNull(LLVMTypeOf(val)), "cast_bool");
           }

           unsigned from_width = LLVMGetIntTypeWidth(LLVMTypeOf(val));
           unsigned to_width = LLVMGetIntTypeWidth(to_type);
           
           if (from_width < to_width) {
               // Extension: Bools and Unsigned Ints use ZExt
               if (from.is_unsigned || from_is_bool) return LLVMBuildZExt(ctx->builder, val, to_type, "cast_zext");
               // Signed Ints use SExt
               return LLVMBuildSExt(ctx->builder, val, to_type, "cast_sext");
           } else if (from_width > to_width) {
               // Truncation
               return LLVMBuildTrunc(ctx->builder, val, to_type, "cast_trunc");
           }
           // Same width
           return val;
      }
      
      // Float -> Float (width change)
      if (from_is_float && to_is_float) {
           return LLVMBuildFPCast(ctx->builder, val, to_type, "cast_fp");
      }

      // Ptr -> Ptr
      if (from.ptr_depth > 0 && to.ptr_depth > 0) {
          return LLVMBuildBitCast(ctx->builder, val, to_type, "cast_ptr");
      }
      
      // Ptr -> Int/Bool
      if (from.ptr_depth > 0 && to_is_int) {
           // Ptr -> Bool (Comparison against null)
           if (to_is_bool) {
               return LLVMBuildICmp(ctx->builder, LLVMIntNE, val, LLVMConstPointerNull(LLVMTypeOf(val)), "cast_ptr_bool");
           }
           return LLVMBuildPtrToInt(ctx->builder, val, to_type, "cast_ptr_int");
      }

      // Int -> Ptr
      if (from_is_int && to.ptr_depth > 0) {
           return LLVMBuildIntToPtr(ctx->builder, val, to_type, "cast_int_ptr");
      }

      return LLVMBuildBitCast(ctx->builder, val, to_type, "cast_generic");
  }
  
  return LLVMConstInt(LLVMInt32Type(), 0, 0);
}
