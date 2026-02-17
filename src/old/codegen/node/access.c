#include "../codegen.h"

LLVMValueRef gen_trait_access(CodegenCtx *ctx, TraitAccessNode *ta) {
      LLVMValueRef obj_ptr = NULL;
      VarType obj_t = codegen_calc_type(ctx, ta->object);
      
      if (obj_t.ptr_depth > 0) obj_ptr = codegen_expr(ctx, ta->object);
      else obj_ptr = codegen_addr(ctx, ta->object);
      
      if (!obj_ptr) codegen_error(ctx, (ASTNode*)ta, "Cannot access trait of null/invalid object");
      if (!obj_t.class_name) codegen_error(ctx, (ASTNode*)ta, "Object type has no class name");
      
      ClassInfo *ci = find_class(ctx, obj_t.class_name);
      if (!ci) codegen_error(ctx, (ASTNode*)ta, "Object is not a class");

      int offset = get_trait_offset(ctx, ci, ta->trait_name);
      if (offset == -1) {
          char msg[256]; snprintf(msg, 256, "Class '%s' does not have trait '%s'", ci->name, ta->trait_name);
          codegen_error(ctx, (ASTNode*)ta, msg);
      }
      
      ClassInfo *trait = find_class(ctx, ta->trait_name);
      LLVMTypeRef trait_type = trait ? trait->struct_type : LLVMStructCreateNamed(LLVMGetGlobalContext(), ta->trait_name);
      
      LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), offset, 0) };
      LLVMValueRef trait_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, obj_ptr, indices, 2, "trait_ptr");
      
      return LLVMBuildBitCast(ctx->builder, trait_ptr, LLVMPointerType(trait_type, 0), "trait_cast");
}

LLVMValueRef gen_array_lit(CodegenCtx *ctx, ArrayLitNode *an) {
    int count = 0; ASTNode *el = an->elements; while(el){count++; el=el->next;}
    
    VarType et = {TYPE_INT, 0, NULL, 0, 0};
    if (an->elements) et = codegen_calc_type(ctx, an->elements);
    
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
    
    LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
    return LLVMBuildGEP2(ctx->builder, arr_type, alloca, indices, 2, "arr_decay");
}

LLVMValueRef gen_literal(CodegenCtx *ctx, LiteralNode *l) {
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

LLVMValueRef gen_array_access(CodegenCtx *ctx, ArrayAccessNode *aa) {
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
        
        LLVMValueRef target_ptr = codegen_addr(ctx, (ASTNode*)aa);
        if (target_ptr) {
            LLVMTypeRef struct_type = get_lvalue_struct_type(ctx, (ASTNode*)aa);
            
            if (struct_type && LLVMGetTypeKind(struct_type) == LLVMArrayTypeKind) {
                 LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
                 return LLVMBuildGEP2(ctx->builder, struct_type, target_ptr, indices, 2, "arr_decay");
            } else if (struct_type) {
                 return LLVMBuildLoad2(ctx->builder, struct_type, target_ptr, "arr_val");
            } else {
                 LLVMTypeRef elem_type = get_llvm_type(ctx, target_t); 
                 return LLVMBuildLoad2(ctx->builder, elem_type, target_ptr, "arr_val");
            }
        }
        
        LLVMValueRef target = codegen_expr(ctx, aa->target); 
        LLVMValueRef index = codegen_expr(ctx, aa->index);
        LLVMTypeRef el_type = get_llvm_type(ctx, target_t); 
        
        if (target_t.array_size > 0) {
             LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), index };
             LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, el_type, target, indices, 2, "arr_idx");
             return LLVMBuildLoad2(ctx->builder, LLVMGetElementType(el_type), gep, "val");
        } 
        else if (target_t.ptr_depth > 0) {
            el_type = LLVMGetElementType(el_type);
            LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, LLVMGetElementType(el_type), target, &index, 1, "ptr_idx");
            return LLVMBuildLoad2(ctx->builder, LLVMGetElementType(el_type), gep, "val");
        }
        return LLVMConstInt(LLVMInt32Type(), 0, 0);
}

LLVMValueRef gen_member_access(CodegenCtx *ctx, MemberAccessNode *ma) {
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
               codegen_error(ctx, (ASTNode*)ma, "Enum member not found in codegen");
           }
      }

      LLVMValueRef addr = codegen_addr(ctx, (ASTNode*)ma);
      if (addr) {
          VarType vt = codegen_calc_type(ctx, (ASTNode*)ma);
          LLVMTypeRef type = get_llvm_type(ctx, vt);

          LLVMTypeRef struct_type = get_lvalue_struct_type(ctx, (ASTNode*)ma);
          if (struct_type && LLVMGetTypeKind(struct_type) == LLVMArrayTypeKind) {
               LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
               return LLVMBuildGEP2(ctx->builder, struct_type, addr, indices, 2, "mem_decay");
          }

          if (vt.array_size > 0 && vt.ptr_depth == 0) {
               LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
               return LLVMBuildGEP2(ctx->builder, get_llvm_type(ctx, vt), addr, indices, 2, "mem_decay");
          }

          return LLVMBuildLoad2(ctx->builder, type, addr, "mem_val");
      }
      return LLVMConstInt(LLVMInt32Type(), 0, 0);
}
