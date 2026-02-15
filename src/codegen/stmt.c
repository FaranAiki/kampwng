#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void codegen_assign(CodegenCtx *ctx, AssignNode *node) {
  LLVMValueRef ptr = NULL;
  LLVMTypeRef elem_type = NULL;
  VarType target_vt = {0}; // Track var type for array checks
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
            target_vt = sym->vtype;
            if (sym->is_array || sym->vtype.array_size > 0) target_is_array = 1;
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
                  LLVMValueRef mem_ptr; 
                  
                  // Handle Union vs Struct
                  if (ci->is_union) {
                      mem_ptr = LLVMBuildBitCast(ctx->builder, this_val, LLVMPointerType(mem_type, 0), "union_mem_ptr");
                  } else {
                      LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                      mem_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, this_val, indices, 2, "implicit_mem_ptr");
                  }
                  
                  ptr = mem_ptr;
                  elem_type = mem_type;
                  target_vt = mvt;
                  if (mvt.array_size > 0) target_is_array = 1;
                  
                  if (node->index) {
                      // Indexing logic...
                      target_is_array = 0; // If indexed, we assign to element, not array
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

  // NEW: Union Assignment Handling
  if (node->op == TOKEN_ASSIGN && target_vt.base == TYPE_CLASS && target_vt.class_name) {
      ClassInfo *ci = find_class(ctx, target_vt.class_name);
      if (ci && ci->is_union) {
          // Calculate RHS type to find matching member
          VarType r_vt = codegen_calc_type(ctx, node->value);
          LLVMValueRef rhs = codegen_expr(ctx, node->value);
          
          ClassMember *m = ci->members;
          ClassMember *best_match = NULL;
          
          while(m) {
              // Exact match 
              if (m->vtype.base == r_vt.base && m->vtype.ptr_depth == r_vt.ptr_depth && m->vtype.array_size == r_vt.array_size) {
                  best_match = m; break;
              }
              // String literal to Char Array
              if (m->vtype.base == TYPE_CHAR && m->vtype.array_size > 0 && 
                 (r_vt.base == TYPE_STRING || (r_vt.base == TYPE_CHAR && r_vt.ptr_depth == 1))) {
                  best_match = m; break;
              }
              // Basic primitive match (int -> int)
              if (m->vtype.base == r_vt.base && m->vtype.ptr_depth == r_vt.ptr_depth) {
                   best_match = m; break;
              }
              m = m->next;
          }
          
          if (best_match) {
               // Special handling for char array copy
               if (best_match->vtype.base == TYPE_CHAR && best_match->vtype.array_size > 0 && 
                  (r_vt.base == TYPE_STRING || (r_vt.base == TYPE_CHAR && r_vt.ptr_depth == 1))) {
                    LLVMValueRef mem_ptr = LLVMBuildBitCast(ctx->builder, ptr, LLVMPointerType(best_match->type, 0), "union_arr_ptr");
                    LLVMValueRef dest = LLVMBuildBitCast(ctx->builder, mem_ptr, LLVMPointerType(LLVMInt8Type(), 0), "dest_cast");
                    LLVMValueRef args[] = { dest, rhs };
                    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args, 2, "");
                    return;
               }
               
               // Standard Store
               LLVMValueRef mem_ptr = LLVMBuildBitCast(ctx->builder, ptr, LLVMPointerType(best_match->type, 0), "union_mem_ptr");
               
               // Handle implicit numeric casts (int -> double etc) if needed
               if (LLVMGetTypeKind(LLVMTypeOf(rhs)) != LLVMGetTypeKind(LLVMGetElementType(LLVMTypeOf(mem_ptr)))) {
                    // Very basic cast, rely on semantic checker mostly
                    if (LLVMGetTypeKind(LLVMGetElementType(LLVMTypeOf(mem_ptr))) == LLVMDoubleTypeKind) {
                         rhs = LLVMBuildSIToFP(ctx->builder, rhs, LLVMDoubleType(), "union_cast");
                    }
               }
               
               LLVMBuildStore(ctx->builder, rhs, mem_ptr);
               return;
          }
      }
  }

  LLVMValueRef rhs = codegen_expr(ctx, node->value);
  LLVMValueRef final_val = rhs;

  if (node->op != TOKEN_ASSIGN) {
    LLVMValueRef lhs_val = LLVMBuildLoad2(ctx->builder, elem_type, ptr, "curr_val");
    final_val = LLVMBuildAdd(ctx->builder, lhs_val, rhs, "compound_tmp");
  }

  // FIX: Array String Assignment (jablay.name = c"bin")
  // If target is array of char, and RHS is char* (string), use strcpy.
  if (target_is_array && target_vt.base == TYPE_CHAR) {
      if (LLVMGetTypeKind(LLVMTypeOf(rhs)) == LLVMPointerTypeKind) {
          LLVMValueRef dest = LLVMBuildBitCast(ctx->builder, ptr, LLVMPointerType(LLVMInt8Type(), 0), "dest_cast");
          LLVMValueRef args[] = { dest, rhs };
          LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args, 2, "");
          return;
      }
  }

  LLVMBuildStore(ctx->builder, final_val, ptr);
}

void codegen_var_decl(CodegenCtx *ctx, VarDeclNode *node) {
  LLVMValueRef alloca = NULL;
  LLVMTypeRef type = NULL;
  VarType symbol_vtype = node->var_type; 
  
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
  // Check if we are inside a Flux function
  if (ctx->flux_promise_val) {
      // Return in Flux = Finish
      LLVMValueRef finished_ptr = LLVMBuildStructGEP2(ctx->builder, ctx->flux_promise_type, ctx->flux_promise_val, 0, "fin_ptr");
      LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt1Type(), 1, 0), finished_ptr);
      
      // Final Suspend with SAVE
      LLVMValueRef save_args[] = { ctx->flux_coro_hdl };
      LLVMValueRef save_tok = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_save), ctx->coro_save, save_args, 1, "ret_save");

      LLVMValueRef args[] = { save_tok, LLVMConstInt(LLVMInt1Type(), 1, 0) };
      LLVMValueRef res = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_suspend), ctx->coro_suspend, args, 2, "final_suspend");
      
      LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
      LLVMBasicBlockRef suspend_bb = LLVMAppendBasicBlock(func, "final_suspend_ret");
      
      // FIX: Switch on raw i8. Use TypeOf(res) to ensure constant type matches.
      LLVMValueRef sw = LLVMBuildSwitch(ctx->builder, res, suspend_bb, 2);
      LLVMAddCase(sw, LLVMConstInt(LLVMTypeOf(res), 0, 0), ctx->flux_return_block);
      LLVMAddCase(sw, LLVMConstInt(LLVMTypeOf(res), 1, 0), ctx->flux_return_block);
      
      // In suspend block, return the coroutine handle
      LLVMPositionBuilderAtEnd(ctx->builder, suspend_bb);
      LLVMBuildRet(ctx->builder, ctx->flux_coro_hdl);
      
      return;
  }

  if (node->value) {
    LLVMValueRef ret = codegen_expr(ctx, node->value);
    LLVMBuildRet(ctx->builder, ret);
  } else {
    LLVMBuildRetVoid(ctx->builder);
  }
}

// code generation for node
void codegen_node(CodegenCtx *ctx, ASTNode *node) {
  while (node) {
  if (node->type == NODE_FUNC_DEF) codegen_func_def(ctx, (FuncDefNode*)node);
  else if (node->type == NODE_RETURN) codegen_return(ctx, (ReturnNode*)node);
  else if (node->type == NODE_CALL) codegen_expr(ctx, node); 
  else if (node->type == NODE_LOOP) codegen_loop(ctx, (LoopNode*)node);
  else if (node->type == NODE_WHILE) codegen_while(ctx, (WhileNode*)node);
  else if (node->type == NODE_SWITCH) codegen_switch(ctx, (SwitchNode*)node); // Added
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
  else if (node->type == NODE_LINK) { /* used in the main itself */}
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
