#include "../codegen.h"

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
      
      // CHECK FOR FUNCTION POINTER DECAY
      // If the semantic analyzer resolved this to a function, 'mangled_name' will be set.
      if (node->mangled_name) {
           LLVMValueRef func = LLVMGetNamedFunction(ctx->module, node->mangled_name);
           if (func) return func;
           // Fallback to non-mangled name if mangled not found (e.g. extern C)
           func = LLVMGetNamedFunction(ctx->module, name);
           if (func) return func;
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
