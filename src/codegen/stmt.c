#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void codegen_assign(CodegenCtx *ctx, AssignNode *node) {
  LLVMValueRef ptr = NULL;
  LLVMTypeRef elem_type = NULL;

  if (node->target) {
      ptr = codegen_addr(ctx, node->target);
      VarType vt = codegen_calc_type(ctx, node->target);
      elem_type = get_llvm_type(ctx, vt);
  } else {
      Symbol *sym = find_symbol(ctx, node->name);
      
      // If local/global var found
      if (sym) {
          if (node->index) {
            LLVMValueRef idx = codegen_expr(ctx, node->index);
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
              
              VarType vt = sym->vtype;
              if (vt.ptr_depth == 0) {
                  fprintf(stderr, "Error: Cannot index non-pointer variable %s\n", node->name);
                  exit(1);
              }
              vt.ptr_depth--;
              elem_type = get_llvm_type(ctx, vt);
              
              ptr = LLVMBuildGEP2(ctx->builder, elem_type, base, indices, 1, "ptr_elem");
            }
          } else {
            ptr = sym->value;
            elem_type = sym->type;
          }
      } 
      // If not found, check implicit 'this' member
      else {
          Symbol *this_sym = find_symbol(ctx, "this");
          if (this_sym && this_sym->vtype.class_name) {
              ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
              LLVMTypeRef mem_type; VarType mvt;
              int idx = get_member_index(ci, node->name, &mem_type, &mvt);
              
              if (idx != -1) {
                  LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder, this_sym->type, this_sym->value, "this_ptr");
                  LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                  ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, this_val, indices, 2, "implicit_mem_ptr");
                  elem_type = mem_type;
                  
                  if (node->index) {
                      LLVMValueRef idx_val = codegen_expr(ctx, node->index);
                      if (LLVMGetTypeKind(elem_type) == LLVMArrayTypeKind) {
                          LLVMValueRef arr_indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), idx_val };
                          ptr = LLVMBuildGEP2(ctx->builder, elem_type, ptr, arr_indices, 2, "elem_ptr");
                          elem_type = LLVMGetElementType(elem_type);
                      } else {
                          // Pointer field
                          LLVMValueRef base = LLVMBuildLoad2(ctx->builder, elem_type, ptr, "ptr_base");
                          ptr = LLVMBuildGEP2(ctx->builder, LLVMGetElementType(elem_type), base, &idx_val, 1, "ptr_elem");
                          elem_type = LLVMGetElementType(elem_type);
                      }
                  }
              }
          }
          
          if (!ptr) {
              fprintf(stderr, "Error: Assignment to undefined variable %s\n", node->name); 
              exit(1); 
          }
      }
  }

  LLVMValueRef rhs = codegen_expr(ctx, node->value);
  LLVMValueRef final_val = rhs;

  if (node->op != TOKEN_ASSIGN) {
    LLVMValueRef lhs_val = LLVMBuildLoad2(ctx->builder, elem_type, ptr, "curr_val");
    final_val = LLVMBuildAdd(ctx->builder, lhs_val, rhs, "compound_tmp");
  }

  LLVMBuildStore(ctx->builder, final_val, ptr);
}

void codegen_var_decl(CodegenCtx *ctx, VarDeclNode *node) {
  LLVMValueRef alloca = NULL;
  LLVMTypeRef type = NULL;

  if (node->is_array) {
      LLVMTypeRef elem_type = (node->var_type.base == TYPE_AUTO) ? LLVMInt32Type() : get_llvm_type(ctx, node->var_type);
      type = LLVMArrayType(elem_type, 10); 
      alloca = LLVMBuildAlloca(ctx->builder, type, node->name);
  } else {
    LLVMValueRef init_val = codegen_expr(ctx, node->initializer);
    
    if (node->var_type.base == TYPE_AUTO) {
      node->var_type = codegen_calc_type(ctx, node->initializer);
      type = get_llvm_type(ctx, node->var_type);
    } else {
      type = get_llvm_type(ctx, node->var_type);
    }
    
    alloca = LLVMBuildAlloca(ctx->builder, type, node->name);
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
  else if (node->type == NODE_MEMBER_ACCESS) codegen_expr(ctx, node);
  else if (node->type == NODE_METHOD_CALL) codegen_expr(ctx, node); 
  else if (node->type == NODE_LINK) { }
  node = node->next;
  }
}
