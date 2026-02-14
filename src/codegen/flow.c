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
      // Return Safe Zero Memory Representation for everything instead of just throwing i32 0. Fixes implicit fallback validation.
      LLVMBuildRet(ctx->builder, LLVMConstNull(ret_type));
    }
  }
  
  ctx->symbols = saved_scope; 
  if (prev_block) LLVMPositionBuilderAtEnd(ctx->builder, prev_block);
}

// loop [int] {}
void codegen_loop(CodegenCtx *ctx, LoopNode *node) {
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

// while or while once
void codegen_while(CodegenCtx *ctx, WhileNode *node) {
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

// switch ()
void codegen_switch(CodegenCtx *ctx, SwitchNode *node) {
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
    }
    LLVMBuildBr(ctx->builder, ctx->current_loop->break_target);
}

void codegen_continue(CodegenCtx *ctx) {
    if (!ctx->current_loop || !ctx->current_loop->continue_target) {
        fprintf(stderr, "Error: 'continue' outside of loop\n");
    }
    LLVMBuildBr(ctx->builder, ctx->current_loop->continue_target);
}

void codegen_if(CodegenCtx *ctx, IfNode *node) {
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

// --- FLUX (COROUTINE) CODE GENERATION ---

// Refactored using llvm.coro intrinsics (C++20 Style)

void codegen_flux_def(CodegenCtx *ctx, FuncDefNode *node) {
    int param_count = 0;
    Parameter *p = node->params;
    while(p) { param_count++; p = p->next; }
    
    // Create Promise Type: { i1 finished, YieldType val }
    LLVMTypeRef yield_type = get_llvm_type(ctx, node->ret_type);
    LLVMTypeRef promise_struct_elems[] = { LLVMInt1Type(), yield_type };
    LLVMTypeRef promise_type = LLVMStructType(promise_struct_elems, 2, false);
    
    // Function signature: Returns i8* (coroutine handle)
    LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * param_count);
    p = node->params;
    int idx = 0;
    for(int i=0; i<param_count; i++) {
        param_types[i] = get_llvm_type(ctx, p->type);
        p = p->next;
    }
    LLVMTypeRef func_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), param_types, param_count, false);
    
    const char *func_name = node->mangled_name ? node->mangled_name : node->name;
    LLVMValueRef func = LLVMAddFunction(ctx->module, func_name, func_type);
    free(param_types);

    // Basic Blocks
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    // Removed unused alloc_bb which caused terminator error
    LLVMBasicBlockRef begin_bb = LLVMAppendBasicBlock(func, "begin");
    LLVMBasicBlockRef cleanup_bb = LLVMAppendBasicBlock(func, "cleanup");
    LLVMBasicBlockRef suspend_bb = LLVMAppendBasicBlock(func, "suspend");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "body");
    
    LLVMPositionBuilderAtEnd(ctx->builder, entry);
    
    // 1. Setup Promise and Coro ID
    LLVMValueRef promise = LLVMBuildAlloca(ctx->builder, promise_type, "promise");
    LLVMValueRef null_ptr = LLVMConstPointerNull(LLVMPointerType(LLVMInt8Type(), 0));
    LLVMValueRef promise_ptr = LLVMBuildBitCast(ctx->builder, promise, LLVMPointerType(LLVMInt8Type(), 0), "promise_void");
    
    // llvm.coro.id(align, promise, null, null)
    LLVMValueRef id_args[] = { 
        LLVMConstInt(LLVMInt32Type(), 0, 0), 
        promise_ptr, 
        null_ptr, 
        null_ptr 
    };
    LLVMValueRef id = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_id), ctx->coro_id, id_args, 4, "id");
    
    // 2. Allocation Logic
    // For simplicity, we just malloc always, but typical llvm.coro uses llvm.coro.alloc intrinsic check
    // However, LLVM default pipeline often requires llvm.coro.alloc usage to optimize out allocations.
    // %size = call i64 @llvm.coro.size.i64()
    // %mem = call i8* @malloc(i64 %size)
    LLVMValueRef size = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_size), ctx->coro_size, NULL, 0, "size");
    LLVMValueRef mem = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "mem");
    LLVMBuildBr(ctx->builder, begin_bb);
    
    LLVMPositionBuilderAtEnd(ctx->builder, begin_bb);
    
    // 3. Coro Begin
    LLVMValueRef begin_args[] = { id, mem };
    LLVMValueRef hdl = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_begin), ctx->coro_begin, begin_args, 2, "hdl");
    
    // STORE HDL for emit/return/suspend
    ctx->flux_coro_hdl = hdl;

    // 4. Params to Locals
    Symbol *saved_syms = ctx->symbols;
    p = node->params;
    for(int i=0; i<param_count; i++) {
        LLVMValueRef arg = LLVMGetParam(func, i);
        LLVMTypeRef pt = get_llvm_type(ctx, p->type);
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, pt, p->name);
        LLVMBuildStore(ctx->builder, arg, alloca);
        add_symbol(ctx, p->name, alloca, pt, p->type, 0, 1);
        p = p->next;
    }

    // 5. Initial Suspend (Generators usually suspend initially or return handle)
    // suspend -> ret hdl, resume -> body, cleanup -> cleanup_bb
    
    // llvm.coro.save required before suspend
    LLVMValueRef save_hdl_args[] = { hdl };
    LLVMValueRef save_tok = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_save), ctx->coro_save, save_hdl_args, 1, "save_tok");
    
    LLVMValueRef suspend_args[] = { save_tok, LLVMConstInt(LLVMInt1Type(), 0, 0) }; // false = final? no
    LLVMValueRef suspend_res = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_suspend), ctx->coro_suspend, suspend_args, 2, "initial_suspend");
    
    LLVMBasicBlockRef wake_bb = body_bb; 
    
    LLVMValueRef switch_inst = LLVMBuildSwitch(ctx->builder, suspend_res, suspend_bb, 2);
    LLVMAddCase(switch_inst, LLVMConstInt(LLVMInt8Type(), 0, 0), wake_bb);
    LLVMAddCase(switch_inst, LLVMConstInt(LLVMInt8Type(), 1, 0), cleanup_bb);
    
    // Suspend Path: Return handle
    LLVMPositionBuilderAtEnd(ctx->builder, suspend_bb);
    LLVMBuildRet(ctx->builder, hdl);
    
    // Cleanup Path
    LLVMPositionBuilderAtEnd(ctx->builder, cleanup_bb);
    LLVMValueRef free_args[] = { id, hdl };
    LLVMValueRef mem_to_free = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_free), ctx->coro_free, free_args, 2, "mem_free");
    
    LLVMValueRef free_call_args[] = { mem_to_free };
    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->free_func), ctx->free_func, free_call_args, 1, "");
    LLVMBuildRet(ctx->builder, LLVMConstPointerNull(LLVMPointerType(LLVMInt8Type(), 0))); // Should be unreachable logic-wise from caller
    
    // Body Path
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    
    // Save Promise context for emit/return
    ctx->flux_promise_val = promise; 
    ctx->flux_return_block = cleanup_bb; // Actually, return means final suspend

    codegen_node(ctx, node->body);
    
    // Fallthrough: Implicit Return
    // Set finished = true
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMValueRef finished_ptr = LLVMBuildStructGEP2(ctx->builder, promise_type, promise, 0, "finished_ptr");
        LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt1Type(), 1, 0), finished_ptr);
        
        // Final Suspend
        // SAVE TOKEN
        LLVMValueRef final_save_args[] = { hdl };
        LLVMValueRef final_save = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_save), ctx->coro_save, final_save_args, 1, "final_save");

        LLVMValueRef final_suspend_args[] = { final_save, LLVMConstInt(LLVMInt1Type(), 1, 0) }; // true = final
        LLVMValueRef final_res = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_suspend), ctx->coro_suspend, final_suspend_args, 2, "final_suspend");
        
        LLVMValueRef final_sw = LLVMBuildSwitch(ctx->builder, final_res, suspend_bb, 2);
        LLVMAddCase(final_sw, LLVMConstInt(LLVMInt8Type(), 0, 0), cleanup_bb); // Should trap actually
        LLVMAddCase(final_sw, LLVMConstInt(LLVMInt8Type(), 1, 0), cleanup_bb);
    }

    ctx->symbols = saved_syms;
    ctx->flux_promise_val = NULL;
    ctx->flux_return_block = NULL;
    ctx->flux_coro_hdl = NULL;
}

void codegen_emit(CodegenCtx *ctx, EmitNode *node) {
    if (!ctx->flux_promise_val) {
        codegen_error(ctx, (ASTNode*)node, "emit used outside of flux function");
    }

    LLVMValueRef val = codegen_expr(ctx, node->value);
    
    // 1. Store value in promise
    LLVMTypeRef promise_type = LLVMGetElementType(LLVMTypeOf(ctx->flux_promise_val));
    LLVMValueRef val_ptr = LLVMBuildStructGEP2(ctx->builder, promise_type, ctx->flux_promise_val, 1, "val_ptr");
    LLVMBuildStore(ctx->builder, val, val_ptr);
    
    // 2. Set finished = false
    LLVMValueRef fin_ptr = LLVMBuildStructGEP2(ctx->builder, promise_type, ctx->flux_promise_val, 0, "fin_ptr");
    LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt1Type(), 0, 0), fin_ptr);
    
    // 3. Suspend
    // SAVE TOKEN from current hdl
    LLVMValueRef save_args[] = { ctx->flux_coro_hdl };
    LLVMValueRef save_tok = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_save), ctx->coro_save, save_args, 1, "emit_save");

    LLVMValueRef suspend_args[] = { save_tok, LLVMConstInt(LLVMInt1Type(), 0, 0) };
    LLVMValueRef suspend_res = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_suspend), ctx->coro_suspend, suspend_args, 2, "yield_suspend");
    
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef resume_bb = LLVMAppendBasicBlock(func, "after_yield");
    LLVMBasicBlockRef cleanup_bb = LLVMAppendBasicBlock(func, "yield_cleanup"); // Reuse or new?
    // Note: We need to jump to the function's cleanup block if suspend returns 1
    // But getting reference to it is hard without tracking it in ctx.
    // For now, let's just make a trap/unreachable for cleanup path here or logic to free.
    // Better: If suspend returns 1, it means we are being destroyed. We should return from the function (which eventually hits coro.end).
    
    // Handle Suspend Result
    LLVMBasicBlockRef suspend_ret_bb = LLVMAppendBasicBlock(func, "suspend_ret");
    
    LLVMValueRef sw = LLVMBuildSwitch(ctx->builder, suspend_res, suspend_ret_bb, 2);
    LLVMAddCase(sw, LLVMConstInt(LLVMInt8Type(), 0, 0), resume_bb);
    LLVMAddCase(sw, LLVMConstInt(LLVMInt8Type(), 1, 0), cleanup_bb);
    
    // Suspend Path: Need to return the handle? 
    // In `codegen_flux_def`, the initial path returned the handle.
    // Subsequent suspends just return void (control flow back to resume).
    // The coro.suspend intrinsic handles the jump to the return block if we wire it up, 
    // but in raw IR, -1 means "suspend", so we must return from the function.
    // Since the function signature returns i8*, we return Undef or Null here (value is ignored by resume caller).
    LLVMPositionBuilderAtEnd(ctx->builder, suspend_ret_bb);
    LLVMBuildRet(ctx->builder, LLVMConstPointerNull(LLVMPointerType(LLVMInt8Type(), 0)));

    // Cleanup Path
    LLVMPositionBuilderAtEnd(ctx->builder, cleanup_bb);
    // Ideally jump to main cleanup, but here just return.
    LLVMBuildRet(ctx->builder, LLVMConstPointerNull(LLVMPointerType(LLVMInt8Type(), 0)));

    // Resume Path
    LLVMPositionBuilderAtEnd(ctx->builder, resume_bb);
}

void codegen_for_in(CodegenCtx *ctx, ForInNode *node) {
    LLVMValueRef col = codegen_expr(ctx, node->collection);
    VarType col_type = codegen_calc_type(ctx, node->collection);
    
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "for_cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "for_body");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlock(func, "for_end");
    
    // Iterate Logic
    int is_flux = 0;
    
    // Detect if collection is a flux generator call
    LLVMValueRef iter_ptr = NULL;
    LLVMValueRef flux_hdl = NULL;

    // FIX: Determine Yield Type for Flux BEFORE generating promise code or variable allocation
    // Semantic analysis may have incorrectly set node->iter_type to the Generator Context (opaque struct)
    // instead of the yield type. We try to resolve the real yield type from the function symbol.
    VarType iter_alk_type = node->iter_type;
    
    // Prioritize detecting Flux via CALL node symbol lookup
    if (node->collection->type == NODE_CALL) {
        CallNode *cn = (CallNode*)node->collection;
        const char *fname = cn->mangled_name ? cn->mangled_name : cn->name;
        FuncSymbol *fs = find_func_symbol(ctx, fname);
        if (fs && fs->is_flux) {
             is_flux = 1;
             flux_hdl = col;
             iter_alk_type = fs->yield_type;
        }
    }
    
    if (!is_flux) {
        if (col_type.base == TYPE_STRING || (col_type.base == TYPE_CHAR && col_type.ptr_depth == 1)) {
            iter_ptr = LLVMBuildAlloca(ctx->builder, LLVMPointerType(LLVMInt8Type(), 0), "str_iter");
            LLVMBuildStore(ctx->builder, col, iter_ptr);
        } else if (col_type.base == TYPE_INT && col_type.array_size == 0 && col_type.ptr_depth == 0) {
            // Range
            iter_ptr = LLVMBuildAlloca(ctx->builder, LLVMInt64Type(), "range_i");
            LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt64Type(), 0, 0), iter_ptr);
        } else {
            // Flux Generator Fallback (variable handle etc.)
            is_flux = 1;
            flux_hdl = col; // The handle
        }
    }
    
    LLVMBuildBr(ctx->builder, cond_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    
    LLVMValueRef current_val = NULL;
    LLVMValueRef condition = NULL;
    
    if (is_flux) {
        // Resume
        LLVMValueRef res_args[] = { flux_hdl };
        LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_resume), ctx->coro_resume, res_args, 1, "");
        
        // Access Promise
        // Need to know the Promise Type.
        // { i1 finished, YieldType val }
        // Use corrected iter_alk_type to ensure we don't try to embed unsized struct
        LLVMTypeRef val_type = get_llvm_type(ctx, iter_alk_type);
        LLVMTypeRef prom_struct_elems[] = { LLVMInt1Type(), val_type };
        LLVMTypeRef prom_type = LLVMStructType(prom_struct_elems, 2, false);
        
        LLVMValueRef align = LLVMConstInt(LLVMInt32Type(), 0, 0); // Alignment
        LLVMValueRef from_hdl = LLVMConstInt(LLVMInt1Type(), 1, 0); // True
        LLVMValueRef prom_args[] = { flux_hdl, align, from_hdl };
        
        LLVMValueRef prom_void_ptr = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_promise), ctx->coro_promise, prom_args, 3, "prom_ptr_void");
        LLVMValueRef prom_ptr = LLVMBuildBitCast(ctx->builder, prom_void_ptr, LLVMPointerType(prom_type, 0), "prom_ptr");
        
        // Check Finished
        LLVMValueRef fin_ptr = LLVMBuildStructGEP2(ctx->builder, prom_type, prom_ptr, 0, "fin_ptr");
        LLVMValueRef is_finished = LLVMBuildLoad2(ctx->builder, LLVMInt1Type(), fin_ptr, "is_finished");
        
        // Loop condition: !finished
        condition = LLVMBuildNot(ctx->builder, is_finished, "continue");
        
        // Get Value
        LLVMValueRef val_ptr = LLVMBuildStructGEP2(ctx->builder, prom_type, prom_ptr, 1, "val_ptr");
        current_val = LLVMBuildLoad2(ctx->builder, val_type, val_ptr, "val");
    } 
    else if (col_type.base == TYPE_INT) {
        LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), iter_ptr, "idx");
        LLVMValueRef limit = LLVMBuildIntCast(ctx->builder, col, LLVMInt64Type(), "limit");
        condition = LLVMBuildICmp(ctx->builder, LLVMIntSLT, idx, limit, "chk");
        current_val = LLVMBuildIntCast(ctx->builder, idx, LLVMInt32Type(), "val");
    }
    else {
        LLVMValueRef p = LLVMBuildLoad2(ctx->builder, LLVMPointerType(LLVMInt8Type(), 0), iter_ptr, "p");
        LLVMValueRef c = LLVMBuildLoad2(ctx->builder, LLVMInt8Type(), p, "char");
        condition = LLVMBuildICmp(ctx->builder, LLVMIntNE, c, LLVMConstInt(LLVMInt8Type(), 0, 0), "chk");
        current_val = c;
    }
    
    LLVMBuildCondBr(ctx->builder, condition, body_bb, end_bb);
    
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    
    // Assign Loop Var
    // Use corrected iter_alk_type
    LLVMTypeRef var_type = get_llvm_type(ctx, iter_alk_type);
    LLVMValueRef var_alloca = LLVMBuildAlloca(ctx->builder, var_type, node->var_name);
    LLVMBuildStore(ctx->builder, current_val, var_alloca);
    
    Symbol *saved_syms = ctx->symbols;
    add_symbol(ctx, node->var_name, var_alloca, var_type, iter_alk_type, 0, 0);
    
    push_loop_ctx(ctx, cond_bb, end_bb);
    codegen_node(ctx, node->body);
    pop_loop_ctx(ctx);
    
    // Step
    if (!is_flux) {
        if (col_type.base == TYPE_INT) {
            LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), iter_ptr, "idx");
            LLVMValueRef nxt = LLVMBuildAdd(ctx->builder, idx, LLVMConstInt(LLVMInt64Type(), 1, 0), "inc");
            LLVMBuildStore(ctx->builder, nxt, iter_ptr);
        } else {
            LLVMValueRef p = LLVMBuildLoad2(ctx->builder, LLVMPointerType(LLVMInt8Type(), 0), iter_ptr, "p");
            LLVMValueRef nxt = LLVMBuildGEP2(ctx->builder, LLVMInt8Type(), p, (LLVMValueRef[]){LLVMConstInt(LLVMInt64Type(), 1, 0)}, 1, "inc");
            LLVMBuildStore(ctx->builder, nxt, iter_ptr);
        }
    }
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, cond_bb);
    }
    
    ctx->symbols = saved_syms;
    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    
    // Cleanup Flux
    if (is_flux) {
        LLVMValueRef des_args[] = { flux_hdl };
        LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->coro_destroy), ctx->coro_destroy, des_args, 1, "");
    }
}
