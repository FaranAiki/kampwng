#include "../codegen.h"

LLVMValueRef gen_inc_dec(CodegenCtx *ctx, IncDecNode *id) {
      LLVMValueRef addr = codegen_addr(ctx, id->target);
      if (!addr) {
          codegen_error(ctx, (ASTNode*)id, "Operand must be an l-value");
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

// Shared helper for Binary Ops (used by binary expression and compound assignment)
LLVMValueRef llvm_build_bin_op(CodegenCtx *ctx, LLVMValueRef l, LLVMValueRef r, int op, VarType lt, VarType rt) {
      if (lt.base == TYPE_STRING && rt.base == TYPE_STRING && op == TOKEN_PLUS) {
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
      
      if (lt.base == TYPE_STRING && rt.base == TYPE_STRING && (op == TOKEN_EQ || op == TOKEN_NEQ)) {
             LLVMValueRef args[] = {l, r};
             LLVMValueRef cmp_res = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcmp_func), ctx->strcmp_func, args, 2, "cmp_res");
             
             if (op == TOKEN_EQ) 
                 return LLVMBuildICmp(ctx->builder, LLVMIntEQ, cmp_res, LLVMConstInt(LLVMInt32Type(), 0, 0), "str_eq");
             else
                 return LLVMBuildICmp(ctx->builder, LLVMIntNE, cmp_res, LLVMConstInt(LLVMInt32Type(), 0, 0), "str_neq");
      }

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
              // Fallback bitcast (dangerous if types mismatch in size)
              if (LLVMGetTypeKind(LLVMTypeOf(l)) == LLVMGetTypeKind(LLVMTypeOf(r))) {
                  // Only bitcast if kinds match (e.g. ptr to ptr)
                   r = LLVMBuildBitCast(ctx->builder, r, LLVMTypeOf(l), "cast");
              }
          }
      }

      int is_float = (lt.base == TYPE_FLOAT || lt.base == TYPE_DOUBLE || lt.base == TYPE_LONG_DOUBLE || 
                      rt.base == TYPE_FLOAT || rt.base == TYPE_DOUBLE || rt.base == TYPE_LONG_DOUBLE);
      int is_unsigned = (lt.is_unsigned || rt.is_unsigned);

      if (op == TOKEN_PLUS) {
          if (is_float) return LLVMBuildFAdd(ctx->builder, l, r, "fadd");
          return LLVMBuildAdd(ctx->builder, l, r, "add");
      }
      if (op == TOKEN_MINUS) {
          if (is_float) return LLVMBuildFSub(ctx->builder, l, r, "fsub");
          return LLVMBuildSub(ctx->builder, l, r, "sub");
      }
      if (op == TOKEN_STAR) {
          if (is_float) return LLVMBuildFMul(ctx->builder, l, r, "fmul");
          return LLVMBuildMul(ctx->builder, l, r, "mul");
      }
      if (op == TOKEN_SLASH) {
          if (is_float) return LLVMBuildFDiv(ctx->builder, l, r, "fdiv");
          if (is_unsigned) return LLVMBuildUDiv(ctx->builder, l, r, "udiv");
          return LLVMBuildSDiv(ctx->builder, l, r, "sdiv");
      }
      if (op == TOKEN_MOD) {
          if (is_float) return LLVMBuildFRem(ctx->builder, l, r, "frem");
          if (is_unsigned) return LLVMBuildURem(ctx->builder, l, r, "urem");
          return LLVMBuildSRem(ctx->builder, l, r, "srem");
      }
      if (op == TOKEN_LT) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOLT, l, r, "flt");
          if (is_unsigned) return LLVMBuildICmp(ctx->builder, LLVMIntULT, l, r, "ult");
          return LLVMBuildICmp(ctx->builder, LLVMIntSLT, l, r, "slt");
      }
      if (op == TOKEN_GT) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOGT, l, r, "fgt");
          if (is_unsigned) return LLVMBuildICmp(ctx->builder, LLVMIntUGT, l, r, "ugt");
          return LLVMBuildICmp(ctx->builder, LLVMIntSGT, l, r, "sgt");
      }
      if (op == TOKEN_LTE) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOLE, l, r, "flte");
          if (is_unsigned) return LLVMBuildICmp(ctx->builder, LLVMIntULE, l, r, "ulte");
          return LLVMBuildICmp(ctx->builder, LLVMIntSLE, l, r, "slte");
      }
      if (op == TOKEN_GTE) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOGE, l, r, "fgte");
          if (is_unsigned) return LLVMBuildICmp(ctx->builder, LLVMIntUGE, l, r, "ugte");
          return LLVMBuildICmp(ctx->builder, LLVMIntSGE, l, r, "sgte");
      }
      if (op == TOKEN_EQ) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, l, r, "feq");
          return LLVMBuildICmp(ctx->builder, LLVMIntEQ, l, r, "eq");
      }
      if (op == TOKEN_NEQ) {
          if (is_float) return LLVMBuildFCmp(ctx->builder, LLVMRealONE, l, r, "fneq");
          return LLVMBuildICmp(ctx->builder, LLVMIntNE, l, r, "neq");
      }
      if (op == TOKEN_AND) {
          return LLVMBuildAnd(ctx->builder, l, r, "and");
      }
      if (op == TOKEN_OR) {
          return LLVMBuildOr(ctx->builder, l, r, "or");
      }
      if (op == TOKEN_XOR) {
          return LLVMBuildXor(ctx->builder, l, r, "xor");
      }
      if (op == TOKEN_LSHIFT) {
          return LLVMBuildShl(ctx->builder, l, r, "shl");
      }
      if (op == TOKEN_RSHIFT) {
          // Assume arithmetic shift right for signed types, logical for unsigned
          if (lt.is_unsigned) return LLVMBuildLShr(ctx->builder, l, r, "lshr");
          return LLVMBuildAShr(ctx->builder, l, r, "ashr");
      }

      return LLVMConstInt(LLVMInt32Type(), 0, 0);
}

LLVMValueRef gen_binary_op(CodegenCtx *ctx, BinaryOpNode *op) {
      LLVMValueRef l = codegen_expr(ctx, op->left);
      LLVMValueRef r = codegen_expr(ctx, op->right);
      VarType lt = codegen_calc_type(ctx, op->left);
      VarType rt = codegen_calc_type(ctx, op->right);
      
      return llvm_build_bin_op(ctx, l, r, op->op, lt, rt);
}

LLVMValueRef gen_unary_op(CodegenCtx *ctx, UnaryOpNode *u) {
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
      return LLVMConstInt(LLVMInt32Type(), 0, 0);
}
