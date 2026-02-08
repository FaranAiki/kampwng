#include "codegen.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void push_loop_ctx(CodegenCtx *ctx, LLVMBasicBlockRef cont, LLVMBasicBlockRef brk) {
  LoopContext *lc = malloc(sizeof(LoopContext));
  lc->continue_target = cont;
  lc->break_target = brk;
  lc->parent = ctx->current_loop;
  ctx->current_loop = lc;
}

void pop_loop_ctx(CodegenCtx *ctx) {
  if (!ctx->current_loop) return;
  LoopContext *lc = ctx->current_loop;
  ctx->current_loop = lc->parent;
  free(lc);
}

void codegen_func_def(CodegenCtx *ctx, FuncDefNode *node) {
  int param_count = 0;
  Parameter *p = node->params;
  while(p) { param_count++; p = p->next; }
  
  // Method 'this' injection
  int total_params = param_count;
  if (node->class_name) total_params++;
  
  LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * total_params);
  int idx = 0;
  
  if (node->class_name) {
      ClassInfo *ci = find_class(ctx, node->class_name);
      param_types[idx++] = LLVMPointerType(ci->struct_type, 0);
  }
  
  p = node->params;
  for(; idx<total_params; idx++) {
    param_types[idx] = get_llvm_type(ctx, p->type);
    p = p->next;
  }
  
  LLVMTypeRef ret_type = get_llvm_type(ctx, node->ret_type);
  LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types, total_params, node->is_varargs);
  
  // Use Mangled Name (if available) unless it is main
  const char *func_name = node->name;
  if (node->mangled_name && strcmp(node->name, "main") != 0) {
      func_name = node->mangled_name;
  }
  
  LLVMValueRef func = LLVMAddFunction(ctx->module, func_name, func_type);
  free(param_types);
  
  if (!node->body) return; // Extern

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
  LLVMBasicBlockRef prev_block = LLVMGetInsertBlock(ctx->builder); 
  LLVMPositionBuilderAtEnd(ctx->builder, entry);
  
  Symbol *saved_scope = ctx->symbols;
  
  idx = 0;
  if (node->class_name) {
      LLVMValueRef this_val = LLVMGetParam(func, idx);
      LLVMTypeRef this_type = LLVMPointerType(find_class(ctx, node->class_name)->struct_type, 0);
      LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, this_type, "this");
      LLVMBuildStore(ctx->builder, this_val, alloca);
      
      VarType this_vt = {TYPE_CLASS, 1, strdup(node->class_name)}; // this is T*
      add_symbol(ctx, "this", alloca, this_type, this_vt, 0, 0);
      idx++;
  }
  
  p = node->params;
  for(; idx<total_params; idx++) {
    LLVMValueRef arg_val = LLVMGetParam(func, idx);
    LLVMTypeRef type = get_llvm_type(ctx, p->type);
    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, type, p->name);
    LLVMBuildStore(ctx->builder, arg_val, alloca);
    add_symbol(ctx, p->name, alloca, type, p->type, 0, 1); 
    p = p->next;
  }
  
  codegen_node(ctx, node->body);
  
  if (node->ret_type.base == TYPE_VOID) {
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
      LLVMBuildRetVoid(ctx->builder);
    }
  } else {
     if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
      LLVMBuildRet(ctx->builder, LLVMConstInt(LLVMInt32Type(), 0, 0));
    }
  }
  
  ctx->symbols = saved_scope; 
  if (prev_block) LLVMPositionBuilderAtEnd(ctx->builder, prev_block);
}

void codegen_loop(CodegenCtx *ctx, LoopNode *node) {
  // ... (unchanged) ...
  LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
  LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "loop_cond");
  LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "loop_body");
  LLVMBasicBlockRef step_bb = LLVMAppendBasicBlock(func, "loop_step");
  LLVMBasicBlockRef end_bb = LLVMAppendBasicBlock(func, "loop_end");

  LLVMValueRef counter_ptr = LLVMBuildAlloca(ctx->builder, LLVMInt64Type(), "loop_i");
  LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt64Type(), 0, 0), counter_ptr);
  LLVMBuildBr(ctx->builder, cond_bb);

  LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
  LLVMValueRef cur_i = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), counter_ptr, "i_val");
  LLVMValueRef limit = codegen_expr(ctx, node->iterations);
  
  if (LLVMGetTypeKind(LLVMTypeOf(limit)) != LLVMIntegerTypeKind) {
     limit = LLVMBuildFPToUI(ctx->builder, limit, LLVMInt64Type(), "limit_cast");
  } else {
     limit = LLVMBuildIntCast(ctx->builder, limit, LLVMInt64Type(), "limit_cast");
  }

  LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntULT, cur_i, limit, "cmp");
  LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

  LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
  push_loop_ctx(ctx, step_bb, end_bb);
  codegen_node(ctx, node->body);
  pop_loop_ctx(ctx);
  
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
      LLVMBuildBr(ctx->builder, step_bb);
  }

  LLVMPositionBuilderAtEnd(ctx->builder, step_bb);
  LLVMValueRef cur_i_step = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), counter_ptr, "i_val_step");
  LLVMValueRef next_i = LLVMBuildAdd(ctx->builder, cur_i_step, LLVMConstInt(LLVMInt64Type(), 1, 0), "next_i");
  LLVMBuildStore(ctx->builder, next_i, counter_ptr);
  LLVMBuildBr(ctx->builder, cond_bb);

  LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
}

void codegen_while(CodegenCtx *ctx, WhileNode *node) {
    // ... (unchanged)
  LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
  LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "while_cond");
  LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "while_body");
  LLVMBasicBlockRef end_bb = LLVMAppendBasicBlock(func, "while_end");

  if (node->is_do_while) {
      LLVMBuildBr(ctx->builder, body_bb);
      LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
      push_loop_ctx(ctx, cond_bb, end_bb);
      codegen_node(ctx, node->body);
      pop_loop_ctx(ctx);

      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
          LLVMBuildBr(ctx->builder, cond_bb);
      }
      LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
      LLVMValueRef cond = codegen_expr(ctx, node->condition);
      if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
          cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond, LLVMConstInt(LLVMTypeOf(cond), 0, 0), "to_bool");
      }
      LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);
      
  } else {
      LLVMBuildBr(ctx->builder, cond_bb);
      LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
      LLVMValueRef cond = codegen_expr(ctx, node->condition);
      if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
          cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond, LLVMConstInt(LLVMTypeOf(cond), 0, 0), "to_bool");
      }
      LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);

      LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
      push_loop_ctx(ctx, cond_bb, end_bb);
      codegen_node(ctx, node->body);
      pop_loop_ctx(ctx);
      
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
          LLVMBuildBr(ctx->builder, cond_bb);
      }
  }

  LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
}

void codegen_switch(CodegenCtx *ctx, SwitchNode *node) {
    // ... (unchanged) ...
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMValueRef cond = codegen_expr(ctx, node->condition);
    
    if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind) {
        cond = LLVMBuildIntCast(ctx->builder, cond, LLVMInt32Type(), "switch_cond_cast");
    }

    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlock(func, "switch_end");
    LLVMBasicBlockRef default_bb = LLVMAppendBasicBlock(func, "switch_default");
    
    int case_count = 0;
    ASTNode *c = node->cases;
    while(c) { case_count++; c = c->next; }
    
    LLVMBasicBlockRef *case_bbs = malloc(sizeof(LLVMBasicBlockRef) * case_count);
    for(int i=0; i<case_count; i++) {
        case_bbs[i] = LLVMAppendBasicBlock(func, "case_bb");
    }
    
    LLVMValueRef switch_inst = LLVMBuildSwitch(ctx->builder, cond, default_bb, case_count);
    
    c = node->cases;
    int i = 0;
    while(c) {
        CaseNode *cn = (CaseNode*)c;
        LLVMValueRef val = codegen_expr(ctx, cn->value);
        if (LLVMTypeOf(val) != LLVMTypeOf(cond)) {
            if (LLVMIsConstant(val) && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                unsigned long long raw_val = LLVMConstIntGetZExtValue(val);
                val = LLVMConstInt(LLVMTypeOf(cond), raw_val, 0);
            } else {
                val = LLVMConstBitCast(val, LLVMTypeOf(cond));
            }
        }
        
        LLVMAddCase(switch_inst, val, case_bbs[i]);
        
        LLVMPositionBuilderAtEnd(ctx->builder, case_bbs[i]);
        push_loop_ctx(ctx, NULL, end_bb); 
        codegen_node(ctx, cn->body);
        pop_loop_ctx(ctx);
        
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
            if (cn->is_leak) {
                if (i + 1 < case_count) {
                    LLVMBuildBr(ctx->builder, case_bbs[i+1]);
                } else {
                    LLVMBuildBr(ctx->builder, default_bb);
                }
            } else {
                LLVMBuildBr(ctx->builder, end_bb);
            }
        }
        
        c = c->next;
        i++;
    }
    free(case_bbs);
    
    LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
    if (node->default_case) {
        push_loop_ctx(ctx, NULL, end_bb);
        codegen_node(ctx, node->default_case);
        pop_loop_ctx(ctx);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, end_bb);
    }
    
    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
}

void codegen_break(CodegenCtx *ctx) {
    if (!ctx->current_loop) {
        fprintf(stderr, "Error: 'break' outside of loop or switch\n");
        exit(1);
    }
    LLVMBuildBr(ctx->builder, ctx->current_loop->break_target);
}

void codegen_continue(CodegenCtx *ctx) {
    if (!ctx->current_loop || !ctx->current_loop->continue_target) {
        fprintf(stderr, "Error: 'continue' outside of loop\n");
        exit(1);
    }
    LLVMBuildBr(ctx->builder, ctx->current_loop->continue_target);
}

void codegen_if(CodegenCtx *ctx, IfNode *node) {
    // ... (unchanged)
  LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
  LLVMBasicBlockRef then_bb = LLVMAppendBasicBlock(func, "if_then");
  LLVMBasicBlockRef else_bb = LLVMAppendBasicBlock(func, "if_else");
  LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlock(func, "if_merge");

  LLVMValueRef cond = codegen_expr(ctx, node->condition);
  if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
    cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond, LLVMConstInt(LLVMTypeOf(cond), 0, 0), "to_bool");
  }
  
  LLVMBuildCondBr(ctx->builder, cond, then_bb, else_bb);

  LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
  codegen_node(ctx, node->then_body);
  if (!LLVMGetBasicBlockTerminator(then_bb)) LLVMBuildBr(ctx->builder, merge_bb);

  LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
  if (node->else_body) codegen_node(ctx, node->else_body);
  if (!LLVMGetBasicBlockTerminator(else_bb)) LLVMBuildBr(ctx->builder, merge_bb);

  LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
}
