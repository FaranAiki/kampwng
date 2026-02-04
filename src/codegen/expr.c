#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* format_string(const char* input) {
  if (!input) return NULL;
  size_t len = strlen(input);
  char *new_str = malloc(len + 2);
  strcpy(new_str, input);
  return new_str;
}

LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node) {
  if (!node) return LLVMConstInt(LLVMInt32Type(), 0, 0);

  if (node->type == NODE_LITERAL) {
    LiteralNode *l = (LiteralNode*)node;
    if (l->var_type == VAR_DOUBLE) return LLVMConstReal(LLVMDoubleType(), l->val.double_val);
    if (l->var_type == VAR_BOOL) return LLVMConstInt(LLVMInt1Type(), l->val.int_val, 0);
    if (l->var_type == VAR_CHAR) return LLVMConstInt(LLVMInt8Type(), l->val.int_val, 0); 
    if (l->var_type == VAR_STRING) {
      char *fmt = format_string(l->val.str_val);
      LLVMValueRef gstr = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "str_lit");
      free(fmt);
      return gstr;
    }
    return LLVMConstInt(get_llvm_type(l->var_type), l->val.int_val, 0);
  }
  else if (node->type == NODE_VAR_REF) {
    VarRefNode *r = (VarRefNode*)node;
    Symbol *sym = find_symbol(ctx, r->name);
    if (!sym) { fprintf(stderr, "Error: Undefined variable %s\n", r->name); exit(1); }
    
    if (sym->is_array) {
        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
        return LLVMBuildGEP2(ctx->builder, sym->type, sym->value, indices, 2, "array_decay");
    }
    
    return LLVMBuildLoad2(ctx->builder, sym->type, sym->value, r->name);
  }
  else if (node->type == NODE_ASSIGN) {
      AssignNode *an = (AssignNode*)node;
      
      // 1. Perform the assignment (side effect)
      codegen_assign(ctx, an); 
      
      // 2. Reload and return the new value (result)
      Symbol *sym = find_symbol(ctx, an->name);
      
      LLVMValueRef ptr;
      LLVMTypeRef elem_type;

      if (an->index) {
          LLVMValueRef idx = codegen_expr(ctx, an->index);
          if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
             idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
          } else {
             idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
          }

          if (sym->is_array) {
              LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), idx };
              ptr = LLVMBuildGEP2(ctx->builder, sym->type, sym->value, indices, 2, "elem_ptr");
              elem_type = LLVMGetElementType(sym->type);
          } else {
              LLVMValueRef base = LLVMBuildLoad2(ctx->builder, sym->type, sym->value, "ptr_base");
              LLVMValueRef indices[] = { idx };
              elem_type = LLVMGetElementType(sym->type);
              ptr = LLVMBuildGEP2(ctx->builder, elem_type, base, indices, 1, "ptr_elem");
          }
      } else {
          ptr = sym->value;
          elem_type = sym->type;
      }
      
      return LLVMBuildLoad2(ctx->builder, elem_type, ptr, "assign_res");
  }
  else if (node->type == NODE_INC_DEC) {
    IncDecNode *id = (IncDecNode*)node;
    Symbol *sym = find_symbol(ctx, id->name);
    if (!sym) { fprintf(stderr, "Error: Undefined variable %s\n", id->name); exit(1); }
    if (!sym->is_mutable) { fprintf(stderr, "Error: Cannot increment/decrement immutable variable %s\n", id->name); exit(1); }

    LLVMValueRef ptr;
    LLVMTypeRef elem_type;

    if (id->index) {
        LLVMValueRef idx = codegen_expr(ctx, id->index);
        if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
            idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
        } else {
            idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
        }

        if (sym->is_array) {
             LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), idx };
             ptr = LLVMBuildGEP2(ctx->builder, sym->type, sym->value, indices, 2, "elem_ptr");
             elem_type = LLVMGetElementType(sym->type);
        } else {
             LLVMValueRef base = LLVMBuildLoad2(ctx->builder, sym->type, sym->value, "ptr_base");
             LLVMValueRef indices[] = { idx };
             elem_type = LLVMGetElementType(sym->type);
             ptr = LLVMBuildGEP2(ctx->builder, elem_type, base, indices, 1, "ptr_elem");
        }
    } else {
        ptr = sym->value;
        elem_type = sym->type;
    }

    LLVMValueRef curr = LLVMBuildLoad2(ctx->builder, elem_type, ptr, "curr_val");
    
    LLVMValueRef one;
    int is_float = (LLVMGetTypeKind(LLVMTypeOf(curr)) == LLVMDoubleTypeKind || LLVMGetTypeKind(LLVMTypeOf(curr)) == LLVMFloatTypeKind);
    if (is_float) one = LLVMConstReal(LLVMTypeOf(curr), 1.0);
    else one = LLVMConstInt(LLVMTypeOf(curr), 1, 0);

    LLVMValueRef next;
    if (id->op == TOKEN_INCREMENT) {
        next = is_float ? LLVMBuildFAdd(ctx->builder, curr, one, "inc") : LLVMBuildAdd(ctx->builder, curr, one, "inc");
    } else {
        next = is_float ? LLVMBuildFSub(ctx->builder, curr, one, "dec") : LLVMBuildSub(ctx->builder, curr, one, "dec");
    }

    LLVMBuildStore(ctx->builder, next, ptr);

    return id->is_prefix ? next : curr;
  }
  else if (node->type == NODE_ARRAY_ACCESS) {
    ArrayAccessNode *an = (ArrayAccessNode*)node;
    Symbol *sym = find_symbol(ctx, an->name);
    if (!sym) { fprintf(stderr, "Error: Undefined variable %s\n", an->name); exit(1); }
    
    if (!sym->is_array && LLVMGetTypeKind(sym->type) != LLVMPointerTypeKind) { 
        fprintf(stderr, "Error: %s is not an array or pointer\n", an->name); exit(1); 
    }

    LLVMValueRef idx = codegen_expr(ctx, an->index);
    if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
        idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
    } else {
        idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
    }

    LLVMValueRef ptr;
    LLVMTypeRef elem_type = LLVMGetElementType(sym->type);

    if (sym->is_array) {
        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), idx };
        ptr = LLVMBuildGEP2(ctx->builder, sym->type, sym->value, indices, 2, "elem_ptr");
    } else {
        LLVMValueRef base = LLVMBuildLoad2(ctx->builder, sym->type, sym->value, "ptr_base");
        LLVMValueRef indices[] = { idx };
        ptr = LLVMBuildGEP2(ctx->builder, elem_type, base, indices, 1, "ptr_elem_ptr");
    }
    
    return LLVMBuildLoad2(ctx->builder, elem_type, ptr, "elem_val");
  }
  else if (node->type == NODE_CALL) {
    CallNode *c = (CallNode*)node;
    
    if (strcmp(c->name, "print") == 0) {
      int arg_count = 0;
      ASTNode *curr = c->args;
      while(curr) { arg_count++; curr = curr->next; }
      
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
      curr = c->args;
      for(int i=0; i<arg_count; i++) {
        args[i] = codegen_expr(ctx, curr);
        curr = curr->next;
      }
      
      LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, args, arg_count, "");
      free(args);
      return ret;
    }
    
    if (strcmp(c->name, "input") == 0) {
       if (c->args) {
          LLVMValueRef prompt = codegen_expr(ctx, c->args);
          LLVMValueRef print_args[] = { prompt };
          LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, print_args, 1, "");
       }
       return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->input_func), ctx->input_func, NULL, 0, "input_res");
    }

    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, c->name);
    if (!func) { fprintf(stderr, "Error: Undefined function %s\n", c->name); return LLVMConstInt(LLVMInt32Type(), 0, 0); }
    
    int arg_count = 0;
    ASTNode *curr = c->args;
    while(curr) { arg_count++; curr = curr->next; }
    
    LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
    curr = c->args;
    for(int i=0; i<arg_count; i++) {
      args[i] = codegen_expr(ctx, curr);
      curr = curr->next;
    }
    
    LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
    LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count, "");
    free(args);
    return ret;
  }
  else if (node->type == NODE_UNARY_OP) {
    UnaryOpNode *u = (UnaryOpNode*)node;
    LLVMValueRef operand = codegen_expr(ctx, u->operand);
    
    if (u->op == TOKEN_NOT) {
      if (LLVMGetTypeKind(LLVMTypeOf(operand)) == LLVMIntegerTypeKind) {
        return LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand, LLVMConstInt(LLVMTypeOf(operand), 0, 0), "not");
      } else {
         return LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, operand, LLVMConstReal(LLVMTypeOf(operand), 0.0), "not");
      }
    }
    else if (u->op == TOKEN_BIT_NOT) {
        return LLVMBuildNot(ctx->builder, operand, "bit_not");
    }
    else if (u->op == TOKEN_MINUS) {
       if (LLVMGetTypeKind(LLVMTypeOf(operand)) == LLVMDoubleTypeKind || LLVMGetTypeKind(LLVMTypeOf(operand)) == LLVMFloatTypeKind) {
        return LLVMBuildFNeg(ctx->builder, operand, "neg");
      } else {
        return LLVMBuildNeg(ctx->builder, operand, "neg");
      }
    }
    return operand;
  }
  else if (node->type == NODE_BINARY_OP) {
    BinaryOpNode *op = (BinaryOpNode*)node;
    
    // Short-circuit Logic
    if (op->op == TOKEN_AND_AND || op->op == TOKEN_OR_OR) {
        LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlock(func, "sc_rhs");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlock(func, "sc_merge");
        
        LLVMValueRef lhs = codegen_expr(ctx, op->left);
        // Cast to bool
        if (LLVMGetTypeKind(LLVMTypeOf(lhs)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(lhs)) != 1) {
             lhs = LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs, LLVMConstInt(LLVMTypeOf(lhs), 0, 0), "to_bool");
        }

        LLVMBasicBlockRef lhs_bb = LLVMGetInsertBlock(ctx->builder);
        
        if (op->op == TOKEN_AND_AND) {
             // If lhs is false, jump to merge (result false). Else check rhs.
             LLVMBuildCondBr(ctx->builder, lhs, rhs_bb, merge_bb);
        } else {
             // If lhs is true, jump to merge (result true). Else check rhs.
             LLVMBuildCondBr(ctx->builder, lhs, merge_bb, rhs_bb);
        }
        
        LLVMPositionBuilderAtEnd(ctx->builder, rhs_bb);
        LLVMValueRef rhs = codegen_expr(ctx, op->right);
        // Cast to bool
        if (LLVMGetTypeKind(LLVMTypeOf(rhs)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(rhs)) != 1) {
             rhs = LLVMBuildICmp(ctx->builder, LLVMIntNE, rhs, LLVMConstInt(LLVMTypeOf(rhs), 0, 0), "to_bool");
        }
        LLVMBuildBr(ctx->builder, merge_bb);
        LLVMBasicBlockRef rhs_end_bb = LLVMGetInsertBlock(ctx->builder);

        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(ctx->builder, LLVMInt1Type(), "sc_res");
        
        LLVMValueRef incoming_vals[2];
        LLVMBasicBlockRef incoming_blocks[2];
        
        incoming_vals[0] = rhs;
        incoming_blocks[0] = rhs_end_bb;
        
        if (op->op == TOKEN_AND_AND) {
            incoming_vals[1] = LLVMConstInt(LLVMInt1Type(), 0, 0); // False
        } else {
            incoming_vals[1] = LLVMConstInt(LLVMInt1Type(), 1, 0); // True
        }
        incoming_blocks[1] = lhs_bb;
        
        LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);
        return phi;
    }

    LLVMValueRef l = codegen_expr(ctx, op->left);
    LLVMValueRef r = codegen_expr(ctx, op->right);
    
    LLVMTypeRef l_type = LLVMTypeOf(l);
    LLVMTypeRef r_type = LLVMTypeOf(r);

    int is_ptr_l = (LLVMGetTypeKind(l_type) == LLVMPointerTypeKind);
    int is_ptr_r = (LLVMGetTypeKind(r_type) == LLVMPointerTypeKind);
    
    if (is_ptr_l && is_ptr_r) {
        if (op->op == TOKEN_EQ || op->op == TOKEN_NEQ) {
            LLVMValueRef args[] = { l, r };
            LLVMValueRef diff = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcmp_func), ctx->strcmp_func, args, 2, "strcmp_res");
            if (op->op == TOKEN_EQ) return LLVMBuildICmp(ctx->builder, LLVMIntEQ, diff, LLVMConstInt(LLVMInt32Type(), 0, 0), "str_eq");
            if (op->op == TOKEN_NEQ) return LLVMBuildICmp(ctx->builder, LLVMIntNE, diff, LLVMConstInt(LLVMInt32Type(), 0, 0), "str_neq");
        }
    }

    int is_float = (LLVMGetTypeKind(l_type) == LLVMDoubleTypeKind || LLVMGetTypeKind(r_type) == LLVMDoubleTypeKind ||
            LLVMGetTypeKind(l_type) == LLVMFloatTypeKind || LLVMGetTypeKind(r_type) == LLVMFloatTypeKind);
    
    if (is_float) {
       if (LLVMGetTypeKind(l_type) != LLVMDoubleTypeKind) l = LLVMBuildUIToFP(ctx->builder, l, LLVMDoubleType(), "cast_l");
      if (LLVMGetTypeKind(r_type) != LLVMDoubleTypeKind) r = LLVMBuildUIToFP(ctx->builder, r, LLVMDoubleType(), "cast_r");
      
      switch (op->op) {
        case TOKEN_PLUS: return LLVMBuildFAdd(ctx->builder, l, r, "fadd");
        case TOKEN_MINUS: return LLVMBuildFSub(ctx->builder, l, r, "fsub");
        case TOKEN_STAR: return LLVMBuildFMul(ctx->builder, l, r, "fmul");
        case TOKEN_SLASH: return LLVMBuildFDiv(ctx->builder, l, r, "fdiv");
        case TOKEN_MOD: return LLVMBuildFRem(ctx->builder, l, r, "frem");
        case TOKEN_EQ: return LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, l, r, "feq");
        case TOKEN_NEQ: return LLVMBuildFCmp(ctx->builder, LLVMRealONE, l, r, "fneq");
        case TOKEN_LT: return LLVMBuildFCmp(ctx->builder, LLVMRealOLT, l, r, "flt");
        case TOKEN_GT: return LLVMBuildFCmp(ctx->builder, LLVMRealOGT, l, r, "fgt");
        case TOKEN_LTE: return LLVMBuildFCmp(ctx->builder, LLVMRealOLE, l, r, "fle");
        case TOKEN_GTE: return LLVMBuildFCmp(ctx->builder, LLVMRealOGE, l, r, "fge");
        default: return LLVMConstReal(LLVMDoubleType(), 0.0);
      }
    } else {
       if (LLVMGetTypeKind(l_type) != LLVMGetTypeKind(r_type)) {
        l = LLVMBuildIntCast(ctx->builder, l, LLVMInt32Type(), "cast_l");
        r = LLVMBuildIntCast(ctx->builder, r, LLVMInt32Type(), "cast_r");
      }

      switch (op->op) {
        case TOKEN_PLUS: return LLVMBuildAdd(ctx->builder, l, r, "add");
        case TOKEN_MINUS: return LLVMBuildSub(ctx->builder, l, r, "sub");
        case TOKEN_STAR: return LLVMBuildMul(ctx->builder, l, r, "mul");
        case TOKEN_SLASH: return LLVMBuildSDiv(ctx->builder, l, r, "div");
        case TOKEN_MOD: return LLVMBuildSRem(ctx->builder, l, r, "mod");
        case TOKEN_XOR: return LLVMBuildXor(ctx->builder, l, r, "xor");
        case TOKEN_AND: return LLVMBuildAnd(ctx->builder, l, r, "and");
        case TOKEN_OR:  return LLVMBuildOr(ctx->builder, l, r, "or");
        case TOKEN_LSHIFT: return LLVMBuildShl(ctx->builder, l, r, "shl");
        case TOKEN_RSHIFT: return LLVMBuildAShr(ctx->builder, l, r, "shr");
        case TOKEN_EQ: return LLVMBuildICmp(ctx->builder, LLVMIntEQ, l, r, "eq");
        case TOKEN_NEQ: return LLVMBuildICmp(ctx->builder, LLVMIntNE, l, r, "neq");
        case TOKEN_LT: return LLVMBuildICmp(ctx->builder, LLVMIntSLT, l, r, "lt");
        case TOKEN_GT: return LLVMBuildICmp(ctx->builder, LLVMIntSGT, l, r, "gt");
        case TOKEN_LTE: return LLVMBuildICmp(ctx->builder, LLVMIntSLE, l, r, "le");
        case TOKEN_GTE: return LLVMBuildICmp(ctx->builder, LLVMIntSGE, l, r, "ge");
        default: return LLVMConstInt(LLVMInt32Type(), 0, 0);
      }
    }
  }
  return LLVMConstInt(LLVMInt32Type(), 0, 0);
}
