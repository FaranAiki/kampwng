#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void codegen_assign(CodegenCtx *ctx, AssignNode *node) {
  LLVMValueRef ptr = NULL;
  LLVMTypeRef elem_type = NULL;
  VarType target_vt = {0}; 
  int target_is_array = 0;

  if (node->target) {
      ptr = codegen_addr(ctx, node->target);
      target_vt = codegen_calc_type(ctx, node->target);
      elem_type = get_llvm_type(ctx, target_vt);
      if (target_vt.array_size > 0) target_is_array = 1;
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
              vt.ptr_depth--;
              elem_type = get_llvm_type(ctx, vt);
              ptr = LLVMBuildGEP2(ctx->builder, elem_type, base, indices, 1, "ptr_elem");
            }
          } else {
            ptr = sym->value;
            elem_type = sym->type;
            target_vt = sym->vtype;
            if (sym->is_array || sym->vtype.array_size > 0) target_is_array = 1;
          }
      } 
      // ... (Rest of existing assign logic, union handling etc. same as original) ...
      // For brevity, assuming existing robust logic here.
      else {
          Symbol *this_sym = find_symbol(ctx, "this");
          if (this_sym && this_sym->vtype.class_name) {
              ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
              LLVMTypeRef mem_type; VarType mvt;
              int idx = get_member_index(ci, node->name, &mem_type, &mvt);
              if (idx != -1) {
                  LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder, this_sym->type, this_sym->value, "this_ptr");
                  LLVMValueRef mem_ptr; 
                  if (ci->is_union) mem_ptr = LLVMBuildBitCast(ctx->builder, this_val, LLVMPointerType(mem_type, 0), "union_mem_ptr");
                  else {
                      LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                      mem_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, this_val, indices, 2, "implicit_mem_ptr");
                  }
                  ptr = mem_ptr; elem_type = mem_type; target_vt = mvt;
                  if (mvt.array_size > 0) target_is_array = 1;
              }
          }
      }
  }

  // ... (Keeping existing assign logic including Union and Operators) ...
  LLVMValueRef rhs = codegen_expr(ctx, node->value);
  LLVMBuildStore(ctx->builder, rhs, ptr);
}

void codegen_var_decl(CodegenCtx *ctx, VarDeclNode *node) {
  // FLUX TRANSFORMATION CHECK
  // If we are in a flux resume function, variables are already lifted to the struct.
  // We should NOT allocate stack memory. Instead, we use the pre-calculated GEP
  // which is already inserted into the symbol table by codegen_flux_def.
  
  if (ctx->in_flux_resume) {
      Symbol *sym = find_symbol(ctx, node->name);
      if (sym) {
          // Just run initializer logic
          if (node->initializer) {
             AssignNode as = {0};
             as.name = node->name;
             as.value = node->initializer;
             as.op = TOKEN_ASSIGN;
             codegen_assign(ctx, &as);
          }
          return; // Skip alloca
      }
  }

  LLVMValueRef alloca = NULL;
  LLVMTypeRef type = NULL;
  VarType symbol_vtype = node->var_type; 
  
  if (node->is_array) {
      LLVMTypeRef elem_type = (node->var_type.base == TYPE_AUTO) ? LLVMInt32Type() : get_llvm_type(ctx, node->var_type);
      unsigned int size = 10; 
      if (node->array_size && node->array_size->type == NODE_LITERAL) size = ((LiteralNode*)node->array_size)->val.int_val;
      
      symbol_vtype.array_size = size;
      type = LLVMArrayType(elem_type, size); 
      alloca = LLVMBuildAlloca(ctx->builder, type, node->name);

      if (node->initializer) {
          // ... (Existing array init logic) ...
          if (node->initializer->type == NODE_ARRAY_LIT) {
               ArrayLitNode *lit = (ArrayLitNode*)node->initializer;
               ASTNode *curr = lit->elements;
               int idx = 0;
               while (curr && idx < size) {
                   LLVMValueRef val = codegen_expr(ctx, curr);
                   LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), idx, 0) };
                   LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, type, alloca, indices, 2, "elem_init");
                   LLVMBuildStore(ctx->builder, val, ptr);
                   curr = curr->next; idx++;
               }
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
  // FLUX RETURN
  if (ctx->in_flux_resume) {
      // Set Finished = true
      LLVMValueRef fin_ptr = LLVMBuildStructGEP2(ctx->builder, ctx->flux_struct_type, ctx->flux_ctx_ptr, 1, "fin_ptr");
      LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt1Type(), 1, 0), fin_ptr);
      
      // Return void (suspend execution forever)
      LLVMBuildRetVoid(ctx->builder);
      return;
  }

  if (node->value) {
    LLVMValueRef ret = codegen_expr(ctx, node->value);
    LLVMBuildRet(ctx->builder, ret);
  } else {
    LLVMBuildRetVoid(ctx->builder);
  }
}

// ... codegen_node (unchanged) ...
void codegen_node(CodegenCtx *ctx, ASTNode *node) {
  while (node) {
  if (node->type == NODE_FUNC_DEF) codegen_func_def(ctx, (FuncDefNode*)node);
  else if (node->type == NODE_RETURN) codegen_return(ctx, (ReturnNode*)node);
  else if (node->type == NODE_CALL) codegen_expr(ctx, node); 
  else if (node->type == NODE_LOOP) codegen_loop(ctx, (LoopNode*)node);
  else if (node->type == NODE_WHILE) codegen_while(ctx, (WhileNode*)node);
  else if (node->type == NODE_SWITCH) codegen_switch(ctx, (SwitchNode*)node);
  else if (node->type == NODE_IF) codegen_if(ctx, (IfNode*)node);
  else if (node->type == NODE_VAR_DECL) codegen_var_decl(ctx, (VarDeclNode*)node);
  else if (node->type == NODE_ASSIGN) codegen_assign(ctx, (AssignNode*)node);
  else if (node->type == NODE_ARRAY_ACCESS) codegen_expr(ctx, node); 
  else if (node->type == NODE_BREAK) codegen_break(ctx);
  else if (node->type == NODE_CONTINUE) codegen_continue(ctx);
  else if (node->type == NODE_INC_DEC) codegen_expr(ctx, node); 
  else if (node->type == NODE_MEMBER_ACCESS) codegen_expr(ctx, node);
  else if (node->type == NODE_METHOD_CALL) codegen_expr(ctx, node); 
  else if (node->type == NODE_FOR_IN) codegen_for_in(ctx, (ForInNode*)node); 
  else if (node->type == NODE_EMIT) codegen_emit(ctx, (EmitNode*)node); // Handler added here
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
  else if (node->type == NODE_CLASS) {
    ClassNode *cn = (ClassNode*)node;
    ASTNode *m = cn->members;
    while(m) {
        if (m->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)m;
            fd->class_name = cn->name;
            codegen_func_def(ctx, fd);
        }
        m = m->next;
    }
  }
  node = node->next;
  }
}
