#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void codegen_assign(CodegenCtx *ctx, AssignNode *node) {
  Symbol *sym = find_symbol(ctx, node->name);
  if (!sym) { fprintf(stderr, "Error: Assignment to undefined variable %s\n", node->name); exit(1); }
  
  if (!sym->is_mutable) {
    fprintf(stderr, "Error: Assignment to immutable variable %s\n", node->name);
    exit(1);
  }

  LLVMValueRef ptr;
  LLVMTypeRef elem_type;

  if (node->index) {
    // Array/Pointer Indexing
    if (!sym->is_array && LLVMGetTypeKind(sym->type) != LLVMPointerTypeKind) { 
      fprintf(stderr, "Error: Indexing non-array/non-pointer %s\n", node->name); exit(1); 
    }
    
    LLVMValueRef idx = codegen_expr(ctx, node->index);
    if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
     idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
    } else {
     idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
    }

    if (sym->is_array) {
      LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), idx };
      // Sym->type is ArrayType. GEP2 works with it.
      ptr = LLVMBuildGEP2(ctx->builder, sym->type, sym->value, indices, 2, "elem_ptr");
      elem_type = LLVMGetElementType(sym->type);
    } else {
      // It is a pointer variable. sym->type is T*. sym->value is T** (alloca).
      // Load the pointer T*
      // sym->type is the LLVM type of the variable (T*).
      // We need to load T* from T**.
      LLVMValueRef base = LLVMBuildLoad2(ctx->builder, sym->type, sym->value, "ptr_base");
      LLVMValueRef indices[] = { idx };
      
      // Calculate element type from vtype
      VarType vt = sym->vtype;
      if (vt.ptr_depth > 0) vt.ptr_depth--; // Dereference one level
      elem_type = get_llvm_type(vt);

      ptr = LLVMBuildGEP2(ctx->builder, elem_type, base, indices, 1, "ptr_elem");
    }
  } else {
    // Simple Variable
    ptr = sym->value;
    elem_type = sym->type;
  }

  LLVMValueRef rhs = codegen_expr(ctx, node->value);
  LLVMValueRef final_val = rhs;

  if (node->op != TOKEN_ASSIGN) {
    LLVMValueRef lhs_val = LLVMBuildLoad2(ctx->builder, elem_type, ptr, "curr_val");
    
    LLVMTypeRef l_type = LLVMTypeOf(lhs_val);
    LLVMTypeRef r_type = LLVMTypeOf(rhs);
    
    int is_float = (LLVMGetTypeKind(l_type) == LLVMDoubleTypeKind || LLVMGetTypeKind(r_type) == LLVMDoubleTypeKind ||
            LLVMGetTypeKind(l_type) == LLVMFloatTypeKind || LLVMGetTypeKind(r_type) == LLVMFloatTypeKind);
    
    if (is_float) {
       if (LLVMGetTypeKind(l_type) != LLVMDoubleTypeKind) lhs_val = LLVMBuildSIToFP(ctx->builder, lhs_val, LLVMDoubleType(), "cast_l");
       if (LLVMGetTypeKind(r_type) != LLVMDoubleTypeKind) rhs = LLVMBuildSIToFP(ctx->builder, rhs, LLVMDoubleType(), "cast_r");
    } else {
       if (LLVMGetTypeKind(l_type) != LLVMGetTypeKind(r_type)) {
         lhs_val = LLVMBuildIntCast(ctx->builder, lhs_val, LLVMInt32Type(), "cast_l");
         rhs = LLVMBuildIntCast(ctx->builder, rhs, LLVMInt32Type(), "cast_r");
       }
    }

    switch(node->op) {
      case TOKEN_PLUS_ASSIGN:
        final_val = is_float ? LLVMBuildFAdd(ctx->builder, lhs_val, rhs, "add_tmp") : LLVMBuildAdd(ctx->builder, lhs_val, rhs, "add_tmp"); break;
      case TOKEN_MINUS_ASSIGN: 
        final_val = is_float ? LLVMBuildFSub(ctx->builder, lhs_val, rhs, "sub_tmp") : LLVMBuildSub(ctx->builder, lhs_val, rhs, "sub_tmp"); break;
      case TOKEN_STAR_ASSIGN: 
        final_val = is_float ? LLVMBuildFMul(ctx->builder, lhs_val, rhs, "mul_tmp") : LLVMBuildMul(ctx->builder, lhs_val, rhs, "mul_tmp"); break;
      case TOKEN_SLASH_ASSIGN: 
        final_val = is_float ? LLVMBuildFDiv(ctx->builder, lhs_val, rhs, "div_tmp") : LLVMBuildSDiv(ctx->builder, lhs_val, rhs, "div_tmp"); break;
      case TOKEN_MOD_ASSIGN:
        if (is_float) { final_val = LLVMBuildFRem(ctx->builder, lhs_val, rhs, "mod_tmp"); } 
        else { final_val = LLVMBuildSRem(ctx->builder, lhs_val, rhs, "mod_tmp"); }
        break;
      case TOKEN_XOR_ASSIGN: 
        if (is_float) { fprintf(stderr, "Error: XOR on float\n"); exit(1); }
        final_val = LLVMBuildXor(ctx->builder, lhs_val, rhs, "xor_tmp"); break;
      case TOKEN_AND_ASSIGN:
        if (is_float) { fprintf(stderr, "Error: AND on float\n"); exit(1); }
        final_val = LLVMBuildAnd(ctx->builder, lhs_val, rhs, "and_tmp"); break;
      case TOKEN_OR_ASSIGN:
        if (is_float) { fprintf(stderr, "Error: OR on float\n"); exit(1); }
        final_val = LLVMBuildOr(ctx->builder, lhs_val, rhs, "or_tmp"); break;
      case TOKEN_LSHIFT_ASSIGN:
        if (is_float) { fprintf(stderr, "Error: LSHIFT on float\n"); exit(1); }
        final_val = LLVMBuildShl(ctx->builder, lhs_val, rhs, "shl_tmp"); break;
      case TOKEN_RSHIFT_ASSIGN:
        if (is_float) { fprintf(stderr, "Error: RSHIFT on float\n"); exit(1); }
        final_val = LLVMBuildAShr(ctx->builder, lhs_val, rhs, "shr_tmp"); break;
      default: break;
    }

    if (LLVMTypeOf(final_val) != elem_type) {
       if (LLVMGetTypeKind(elem_type) == LLVMIntegerTypeKind) {
         if (is_float) final_val = LLVMBuildFPToSI(ctx->builder, final_val, elem_type, "cast_back");
         else final_val = LLVMBuildIntCast(ctx->builder, final_val, elem_type, "cast_back");
       } else if (LLVMGetTypeKind(elem_type) == LLVMDoubleTypeKind || LLVMGetTypeKind(elem_type) == LLVMFloatTypeKind) {
         final_val = LLVMBuildFPCast(ctx->builder, final_val, elem_type, "cast_back_float");
       }
    }
  }

  LLVMBuildStore(ctx->builder, final_val, ptr);
}

void codegen_var_decl(CodegenCtx *ctx, VarDeclNode *node) {
  LLVMValueRef alloca = NULL;
  LLVMTypeRef type = NULL;

  if (node->is_array) {
    LLVMTypeRef elem_type = (node->var_type.base == TYPE_AUTO) ? LLVMInt32Type() : get_llvm_type(node->var_type);
    
    int size = 0;
    if (node->array_size) {
      if (node->array_size->type == NODE_LITERAL) {
       size = ((LiteralNode*)node->array_size)->val.int_val;
      } else { size = 10; }
    } else {
      if (node->initializer && node->initializer->type == NODE_ARRAY_LIT) {
       ASTNode *el = ((ArrayLitNode*)node->initializer)->elements;
       while(el) { size++; el = el->next; }
      } else if (node->initializer && node->initializer->type == NODE_LITERAL && ((LiteralNode*)node->initializer)->var_type.base == TYPE_STRING) {
        size = strlen(((LiteralNode*)node->initializer)->val.str_val) + 1;
        elem_type = LLVMInt8Type();
      } else {
       fprintf(stderr, "Error: Array size unknown\n"); exit(1); 
      }
    }
    
    type = LLVMArrayType(elem_type, size);
    alloca = LLVMBuildAlloca(ctx->builder, type, node->name);
    
    if (node->initializer) {
      if (node->initializer->type == NODE_ARRAY_LIT) {
       ASTNode *el = ((ArrayLitNode*)node->initializer)->elements;
       int idx = 0;
       while(el) {
         LLVMValueRef val = codegen_expr(ctx, el);
         LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), idx, 0) };
         LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, type, alloca, indices, 2, "init_ptr");
         LLVMBuildStore(ctx->builder, val, ptr);
         idx++;
         el = el->next;
       }
      } else if (node->initializer->type == NODE_LITERAL && ((LiteralNode*)node->initializer)->var_type.base == TYPE_STRING) {
       char *str = ((LiteralNode*)node->initializer)->val.str_val;
       for (int i = 0; i < size; i++) {
         LLVMValueRef val = LLVMConstInt(LLVMInt8Type(), str[i], 0); 
         LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), i, 0) };
         LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, type, alloca, indices, 2, "str_init_ptr");
         LLVMBuildStore(ctx->builder, val, ptr);
       }
      } else {
       LLVMValueRef val = codegen_expr(ctx, node->initializer);
       if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind && 
         LLVMGetTypeKind(elem_type) == LLVMIntegerTypeKind && 
         LLVMGetIntTypeWidth(elem_type) == 8) {
         
         LLVMValueRef strcpy_func = LLVMGetNamedFunction(ctx->module, "strcpy");
         if (!strcpy_func) {
           LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) };
           LLVMTypeRef ftype = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), args, 2, false);
           strcpy_func = LLVMAddFunction(ctx->module, "strcpy", ftype);
         }
         LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
         LLVMValueRef dest = LLVMBuildGEP2(ctx->builder, type, alloca, indices, 2, "dest_ptr");
         LLVMValueRef args[] = { dest, val };
         LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(strcpy_func), strcpy_func, args, 2, "");
       }
      }
    }
    
  } else {
    LLVMValueRef init_val = codegen_expr(ctx, node->initializer);
    
    if (node->var_type.base == TYPE_AUTO) {
      type = LLVMTypeOf(init_val);
    } else {
      type = get_llvm_type(node->var_type);
    }
    
    alloca = LLVMBuildAlloca(ctx->builder, type, node->name);
    
    if (LLVMGetTypeKind(type) != LLVMGetTypeKind(LLVMTypeOf(init_val))) {
         if (LLVMGetTypeKind(type) == LLVMIntegerTypeKind && LLVMGetTypeKind(LLVMTypeOf(init_val)) == LLVMIntegerTypeKind) {
             init_val = LLVMBuildIntCast(ctx->builder, init_val, type, "init_cast");
         }
    }
    
    LLVMBuildStore(ctx->builder, init_val, alloca);
  }

  add_symbol(ctx, node->name, alloca, type, node->var_type, node->is_array, node->is_mutable);
}

void codegen_return(CodegenCtx *ctx, ReturnNode *node) {
  if (node->value) {
  LLVMValueRef ret = codegen_expr(ctx, node->value);
  LLVMBuildRet(ctx->builder, ret);
  } else {
  LLVMBuildRetVoid(ctx->builder);
  }
}

void codegen_node(CodegenCtx *ctx, ASTNode *node) {
  while (node) {
  if (node->type == NODE_FUNC_DEF) codegen_func_def(ctx, (FuncDefNode*)node);
  else if (node->type == NODE_RETURN) codegen_return(ctx, (ReturnNode*)node);
  else if (node->type == NODE_CALL) codegen_expr(ctx, node); 
  else if (node->type == NODE_LOOP) codegen_loop(ctx, (LoopNode*)node);
  else if (node->type == NODE_WHILE) codegen_while(ctx, (WhileNode*)node);
  else if (node->type == NODE_IF) codegen_if(ctx, (IfNode*)node);
  else if (node->type == NODE_VAR_DECL) codegen_var_decl(ctx, (VarDeclNode*)node);
  else if (node->type == NODE_ASSIGN) codegen_assign(ctx, (AssignNode*)node);
  else if (node->type == NODE_ARRAY_ACCESS) codegen_expr(ctx, node); 
  else if (node->type == NODE_BREAK) codegen_break(ctx);
  else if (node->type == NODE_CONTINUE) codegen_continue(ctx);
  else if (node->type == NODE_INC_DEC) codegen_expr(ctx, node); 
  else if (node->type == NODE_LINK) { }
  node = node->next;
  }
}
