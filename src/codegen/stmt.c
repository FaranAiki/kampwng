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
                  codegen_error(ctx, (ASTNode*)node, "Cannot index non-pointer variable");
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
                          LLVMValueRef base = LLVMBuildLoad2(ctx->builder, elem_type, ptr, "ptr_base");
                          ptr = LLVMBuildGEP2(ctx->builder, LLVMGetElementType(elem_type), base, &idx_val, 1, "ptr_elem");
                          elem_type = LLVMGetElementType(elem_type);
                      }
                  }
              }
          }
          
          if (!ptr) {
              char msg[128];
              snprintf(msg, sizeof(msg), "Assignment to undefined variable '%s'", node->name);
              codegen_error(ctx, (ASTNode*)node, msg);
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
  VarType symbol_vtype = node->var_type; 
  
  // Namespace mangling logic for global vars could go here, 
  // but global vars in LLVM are usually handled in top-level scan.
  // For local vars, no mangling is needed.

  if (node->is_array) {
      LLVMTypeRef elem_type = (node->var_type.base == TYPE_AUTO) ? LLVMInt32Type() : get_llvm_type(ctx, node->var_type);
      unsigned int size = 10; 
      int explicit_size = 0;
      
      if (node->array_size && node->array_size->type == NODE_LITERAL) {
           size = ((LiteralNode*)node->array_size)->val.int_val;
           explicit_size = 1;
      } 
      if (!explicit_size && node->initializer) {
           if (node->initializer->type == NODE_LITERAL) {
               LiteralNode *lit = (LiteralNode*)node->initializer;
               if (lit->var_type.base == TYPE_STRING) {
                    size = strlen(lit->val.str_val) + 1;
               }
           } else if (node->initializer->type == NODE_ARRAY_LIT) {
               ArrayLitNode *lit = (ArrayLitNode*)node->initializer;
               ASTNode *curr = lit->elements;
               int count = 0;
               while (curr) { count++; curr = curr->next; }
               if (count > 0) size = count;
           }
      }
      
      symbol_vtype.array_size = size;
      type = LLVMArrayType(elem_type, size); 
      alloca = LLVMBuildAlloca(ctx->builder, type, node->name);

      if (node->initializer) {
          if (node->initializer->type == NODE_LITERAL) {
               LiteralNode *lit = (LiteralNode*)node->initializer;
               if (lit->var_type.base == TYPE_STRING && node->var_type.base == TYPE_CHAR) {
                    LLVMValueRef str_val = codegen_expr(ctx, node->initializer);
                    LLVMValueRef dest = LLVMBuildBitCast(ctx->builder, alloca, LLVMPointerType(LLVMInt8Type(), 0), "dest_cast");
                    LLVMValueRef args[] = { dest, str_val };
                    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args, 2, "");
                    symbol_vtype.base = TYPE_STRING;
                    symbol_vtype.ptr_depth = 0; 
               }
          } else if (node->initializer->type == NODE_ARRAY_LIT) {
               ArrayLitNode *lit = (ArrayLitNode*)node->initializer;
               ASTNode *curr = lit->elements;
               int idx = 0;
               while (curr) {
                   if (idx >= size) break; 
                   LLVMValueRef val = codegen_expr(ctx, curr);
                   if (LLVMGetTypeKind(elem_type) == LLVMIntegerTypeKind && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMDoubleTypeKind) {
                       val = LLVMBuildFPToUI(ctx->builder, val, elem_type, "cast");
                   } else if (LLVMGetTypeKind(elem_type) == LLVMDoubleTypeKind && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                       val = LLVMBuildUIToFP(ctx->builder, val, elem_type, "cast");
                   }
                   LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), idx, 0) };
                   LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, type, alloca, indices, 2, "elem_init");
                   LLVMBuildStore(ctx->builder, val, ptr);
                   curr = curr->next;
                   idx++;
               }
          }
      } else {
          if (node->var_type.base == TYPE_CHAR) {
              symbol_vtype.base = TYPE_STRING;
              symbol_vtype.ptr_depth = 0;
          }
      }
  } else {
    LLVMValueRef init_val = codegen_expr(ctx, node->initializer);
    
    if (node->var_type.base == TYPE_AUTO) {
      node->var_type = codegen_calc_type(ctx, node->initializer);
      symbol_vtype = node->var_type;
      type = get_llvm_type(ctx, node->var_type);
    } else {
      type = get_llvm_type(ctx, node->var_type);
    }
    
    alloca = LLVMBuildAlloca(ctx->builder, type, node->name);
    LLVMBuildStore(ctx->builder, init_val, alloca);
  }

  add_symbol(ctx, node->name, alloca, type, symbol_vtype, node->is_array, node->is_mutable);
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
  else if (node->type == NODE_NAMESPACE) {
      NamespaceNode *ns = (NamespaceNode*)node;
      char *old_prefix = ctx->current_prefix;
      char new_prefix[256];
      if (old_prefix && strlen(old_prefix) > 0) sprintf(new_prefix, "%s_%s", old_prefix, ns->name);
      else strcpy(new_prefix, ns->name);
      
      ctx->current_prefix = new_prefix;
      codegen_node(ctx, ns->body);
      ctx->current_prefix = old_prefix;
  }
  node = node->next;
  }
}
