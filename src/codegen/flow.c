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

  LLVMValueRef counter_ptr = NULL;
  
  if (ctx->flux_ctx_val) {
      // If inside flux, loop counters need to persist in the struct if yielding
      // For simplicity, we assume generic loop counters are transient unless explicitly declared as vars
      // But standard 'loop' counter is implicit. Let's make it stack allocated.
      // If we yield inside a loop, the stack is lost.
      // FIX: For flux, we ideally need to spill this to the struct.
      // Since this is a "Simple" implementation, we rely on the user to use 'for' or 'while' with explicit vars for robust state.
      counter_ptr = LLVMBuildAlloca(ctx->builder, LLVMInt64Type(), "loop_i");
  } else {
      counter_ptr = LLVMBuildAlloca(ctx->builder, LLVMInt64Type(), "loop_i");
  }
  
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
      
      // If we are in flux, this is a re-entry point potential.
      // But standard while logic is fine as long as vars are persistent.
      
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

// --- FLUX CODE GENERATION ---

// Helper to collect variable declarations in the flux body
void collect_flux_vars(ASTNode *node, LLVMTypeRef *types, char **names, int *count, int *cap, CodegenCtx *ctx) {
    if (!node) return;
    
    if (node->type == NODE_VAR_DECL) {
        VarDeclNode *vd = (VarDeclNode*)node;
        if (*count >= *cap) {
            *cap *= 2;
            types = realloc(types, sizeof(LLVMTypeRef) * (*cap));
            names = realloc(names, sizeof(char*) * (*cap));
        }
        
        // Determine type
        LLVMTypeRef t = get_llvm_type(ctx, vd->var_type);
        if (vd->is_array) {
            int sz = vd->array_size && vd->array_size->type == NODE_LITERAL ? ((LiteralNode*)vd->array_size)->val.int_val : 10;
            t = LLVMArrayType(t, sz);
        }

        names[*count] = strdup(vd->name);
        types[*count] = t;
        (*count)++;
    }
    
    if (node->type == NODE_IF) {
        collect_flux_vars(((IfNode*)node)->then_body, types, names, count, cap, ctx);
        collect_flux_vars(((IfNode*)node)->else_body, types, names, count, cap, ctx);
    } else if (node->type == NODE_WHILE) {
        collect_flux_vars(((WhileNode*)node)->body, types, names, count, cap, ctx);
    } else if (node->type == NODE_LOOP) {
        collect_flux_vars(((LoopNode*)node)->body, types, names, count, cap, ctx);
    } else if (node->type == NODE_FOR_IN) {
        // Handle iterator variable
        ForInNode *f = (ForInNode*)node;
        if (*count >= *cap) {
            *cap *= 2;
            types = realloc(types, sizeof(LLVMTypeRef) * (*cap));
            names = realloc(names, sizeof(char*) * (*cap));
        }
        // Simplified inference for iterator vars (assuming int or char*)
        names[*count] = strdup(f->var_name);
        types[*count] = get_llvm_type(ctx, f->iter_type);
        (*count)++;
        
        collect_flux_vars(f->body, types, names, count, cap, ctx);
    }

    collect_flux_vars(node->next, types, names, count, cap, ctx);
}

// Helper: Transform NODE_RETURN into NODE_BREAK inside flux
// Because standard return logic emits 'ret' which mismatches the state machine return type.
// Break will jump to 'finished' block (state 0) via the dummy loop context.
void replace_returns_with_breaks(ASTNode *node) {
    while (node) {
        if (node->type == NODE_RETURN) {
            node->type = NODE_BREAK;
            // Value is ignored as flux return is effectively void (end of stream)
        }
        
        // Recurse
        if (node->type == NODE_IF) {
            replace_returns_with_breaks(((IfNode*)node)->then_body);
            replace_returns_with_breaks(((IfNode*)node)->else_body);
        } else if (node->type == NODE_WHILE) {
            replace_returns_with_breaks(((WhileNode*)node)->body);
        } else if (node->type == NODE_LOOP) {
            replace_returns_with_breaks(((LoopNode*)node)->body);
        } else if (node->type == NODE_FOR_IN) {
            replace_returns_with_breaks(((ForInNode*)node)->body);
        } else if (node->type == NODE_SWITCH) {
            ASTNode *c = ((SwitchNode*)node)->cases;
            while(c) {
                replace_returns_with_breaks(((CaseNode*)c)->body);
                c = c->next;
            }
            replace_returns_with_breaks(((SwitchNode*)node)->default_case);
        }
        
        node = node->next;
    }
}

// Helper: Rewrite VarDecls to Assignments in Flux Body
// This prevents reallocation of stack variables that should be persistent in the struct.
ASTNode* rewrite_decls_to_assigns(ASTNode *node) {
    if (!node) return NULL;
    
    // Process next first (list tail)
    node->next = rewrite_decls_to_assigns(node->next);
    
    if (node->type == NODE_VAR_DECL) {
        VarDeclNode *vd = (VarDeclNode*)node;
        
        if (vd->initializer) {
            AssignNode *an = calloc(1, sizeof(AssignNode));
            an->base.type = NODE_ASSIGN;
            an->base.next = node->next;
            an->base.line = node->line;
            an->base.col = node->col;
            an->name = strdup(vd->name);
            an->value = vd->initializer;
            an->op = TOKEN_ASSIGN;
            
            // Note: We don't free 'vd' here to avoid double-free if managed elsewhere,
            // but in a proper compiler pass we should.
            
            return (ASTNode*)an;
        } else {
            // No initializer: just skip/remove the node
            // The variable storage is already in the struct.
            return node->next;
        }
    }
    
    // Recurse children
    if (node->type == NODE_IF) {
        ((IfNode*)node)->then_body = rewrite_decls_to_assigns(((IfNode*)node)->then_body);
        ((IfNode*)node)->else_body = rewrite_decls_to_assigns(((IfNode*)node)->else_body);
    } else if (node->type == NODE_WHILE) {
        ((WhileNode*)node)->body = rewrite_decls_to_assigns(((WhileNode*)node)->body);
    } else if (node->type == NODE_LOOP) {
        ((LoopNode*)node)->body = rewrite_decls_to_assigns(((LoopNode*)node)->body);
    } else if (node->type == NODE_FOR_IN) {
        ((ForInNode*)node)->body = rewrite_decls_to_assigns(((ForInNode*)node)->body);
    }
    
    return node;
}

void codegen_flux_def(CodegenCtx *ctx, FuncDefNode *node) {
    // 1. Define the Context Struct
    // Struct { i32 state, params..., locals... }
    
    int param_count = 0;
    Parameter *p = node->params;
    while(p) { param_count++; p = p->next; }
    
    // Locals scan
    int local_cap = 16;
    int local_count = 0;
    LLVMTypeRef *local_types = malloc(sizeof(LLVMTypeRef) * local_cap);
    char **local_names = malloc(sizeof(char*) * local_cap);
    
    collect_flux_vars(node->body, local_types, local_names, &local_count, &local_cap, ctx);

    // Total fields: 1 (state) + params + locals
    int total_fields = 1 + param_count + local_count;
    LLVMTypeRef *struct_elems = malloc(sizeof(LLVMTypeRef) * total_fields);
    
    struct_elems[0] = LLVMInt32Type(); // State
    
    // Params
    p = node->params;
    for(int i=0; i<param_count; i++) {
        struct_elems[1+i] = get_llvm_type(ctx, p->type);
        p = p->next;
    }
    
    // Locals
    for(int i=0; i<local_count; i++) {
        struct_elems[1+param_count+i] = local_types[i];
    }
    
    char struct_name[256];
    snprintf(struct_name, 256, "FluxCtx_%s", node->name);
    LLVMTypeRef ctx_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), struct_name);
    LLVMStructSetBody(ctx_type, struct_elems, total_fields, false);
    
    // SAVE CTX TYPE
    ctx->current_flux_struct_type = ctx_type;
    
    // 2. Generate the Init/Factory Function (The one the user calls)
    // Returns FluxCtx* (Pointer to heap allocated context)
    
    // Params are passed to Init function
    LLVMTypeRef *init_param_types = malloc(sizeof(LLVMTypeRef) * param_count);
    p = node->params;
    for(int i=0; i<param_count; i++) {
        init_param_types[i] = get_llvm_type(ctx, p->type);
        p = p->next;
    }
    
    LLVMTypeRef init_func_type = LLVMFunctionType(LLVMPointerType(ctx_type, 0), init_param_types, param_count, false);
    LLVMValueRef init_func = LLVMAddFunction(ctx->module, node->name, init_func_type);
    
    LLVMBasicBlockRef init_entry = LLVMAppendBasicBlock(init_func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, init_entry);
    
    // Malloc context
    LLVMValueRef size = LLVMSizeOf(ctx_type);
    LLVMValueRef mem = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "ctx_mem");
    LLVMValueRef ctx_ptr = LLVMBuildBitCast(ctx->builder, mem, LLVMPointerType(ctx_type, 0), "ctx");
    
    // Initialize State = 0
    LLVMValueRef state_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_type, ctx_ptr, 0, "state_ptr");
    LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt32Type(), 0, 0), state_ptr);
    
    // Store Params into Context
    for(int i=0; i<param_count; i++) {
        LLVMValueRef arg = LLVMGetParam(init_func, i);
        LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_type, ctx_ptr, 1+i, "param_ptr");
        LLVMBuildStore(ctx->builder, arg, field_ptr);
    }
    
    LLVMBuildRet(ctx->builder, ctx_ptr);
    
    // 3. Generate the Next Function (The state machine)
    // Returns { i1 isValid, T value }
    // Args: FluxCtx*
    
    LLVMTypeRef yield_type = get_llvm_type(ctx, node->ret_type);
    LLVMTypeRef res_struct_elems[] = { LLVMInt1Type(), yield_type };
    LLVMTypeRef res_type = LLVMStructType(res_struct_elems, 2, false);
    
    char next_func_name[256];
    snprintf(next_func_name, 256, "%s_next", node->name);
    
    LLVMTypeRef next_args[] = { LLVMPointerType(ctx_type, 0) };
    LLVMTypeRef next_func_type = LLVMFunctionType(res_type, next_args, 1, false);
    LLVMValueRef next_func = LLVMAddFunction(ctx->module, next_func_name, next_func_type);
    
    LLVMBasicBlockRef next_entry = LLVMAppendBasicBlock(next_func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, next_entry);
    
    LLVMValueRef ctx_arg = LLVMGetParam(next_func, 0);
    ctx->flux_ctx_val = ctx_arg;
    
    // Load State
    LLVMValueRef current_state_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_type, ctx_arg, 0, "state_ptr");
    LLVMValueRef current_state = LLVMBuildLoad2(ctx->builder, LLVMInt32Type(), current_state_ptr, "state");
    
    // Register Symbols pointing to Context GEPs
    // DO THIS BEFORE SWITCH so GEPs are in the entry block before terminator
    Symbol *saved_syms = ctx->symbols;
    
    // Add Params
    p = node->params;
    for(int i=0; i<param_count; i++) {
        LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_type, ctx_arg, 1+i, p->name);
        add_symbol(ctx, p->name, field_ptr, get_llvm_type(ctx, p->type), p->type, 0, 1);
        p = p->next;
    }
    
    // Add Locals
    for(int i=0; i<local_count; i++) {
        LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_type, ctx_arg, 1+param_count+i, local_names[i]);
        VarType vt = {TYPE_UNKNOWN, 0, NULL, 0, 0}; 
        add_symbol(ctx, local_names[i], field_ptr, local_types[i], vt, 0, 1);
    }
    
    // Create Switch
    // Logic start block
    LLVMBasicBlockRef start_bb = LLVMAppendBasicBlock(next_func, "start_logic");
    LLVMBasicBlockRef default_bb = LLVMAppendBasicBlock(next_func, "finished");
    
    LLVMValueRef switch_inst = LLVMBuildSwitch(ctx->builder, current_state, default_bb, 10);
    LLVMAddCase(switch_inst, LLVMConstInt(LLVMInt32Type(), 0, 0), start_bb);
    ctx->current_switch_inst = switch_inst;
    ctx->next_flux_state = 1;

    // Fix 1: Replace RETURN with BREAK to avoid bad return types and terminator issues
    replace_returns_with_breaks(node->body);

    // Fix 2: Rewrite VarDecls to Assignments to use struct fields instead of stack allocas
    node->body = rewrite_decls_to_assigns(node->body);

    // Push a dummy loop context so BREAK commands (from replaced returns) jump to default_bb (finished)
    push_loop_ctx(ctx, default_bb, default_bb);

    // Generate Body
    LLVMPositionBuilderAtEnd(ctx->builder, start_bb);
    codegen_node(ctx, node->body);
    
    pop_loop_ctx(ctx);

    // Fallthrough to finish
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, default_bb);
    }
    
    // Finish Block (Return {0, undef})
    LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
    LLVMValueRef undef_ret = LLVMGetUndef(res_type);
    LLVMValueRef ret_0 = LLVMBuildInsertValue(ctx->builder, undef_ret, LLVMConstInt(LLVMInt1Type(), 0, 0), 0, "set_valid");
    LLVMBuildRet(ctx->builder, ret_0);

    // Cleanup
    ctx->symbols = saved_syms;
    ctx->current_switch_inst = NULL;
    ctx->flux_ctx_val = NULL;
    
    ctx->current_flux_struct_type = NULL;
    
    free(struct_elems);
    free(init_param_types);
    free(local_types);
    free(local_names);
}

void codegen_emit(CodegenCtx *ctx, EmitNode *node) {
    if (!ctx->current_switch_inst) {
        codegen_error(ctx, (ASTNode*)node, "emit used outside of flux function");
    }

    LLVMValueRef val = codegen_expr(ctx, node->value);
    int next_state = ctx->next_flux_state++;
    
    // Update State in Context
    LLVMValueRef ctx_ptr = ctx->flux_ctx_val;
    
    // FIX SEGV: Use cached struct type instead of deriving from pointer
    if (!ctx->current_flux_struct_type) {
         codegen_error(ctx, (ASTNode*)node, "Internal Error: Emit used without flux struct type context");
    }
    LLVMTypeRef ctx_type = ctx->current_flux_struct_type;
    
    LLVMValueRef state_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_type, ctx_ptr, 0, "state_ptr");
    LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt32Type(), next_state, 0), state_ptr);
    
    // Return {1, val}
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMTypeRef res_type = LLVMGetReturnType(LLVMGlobalGetValueType(func));
    
    LLVMValueRef undef = LLVMGetUndef(res_type);
    LLVMValueRef res_1 = LLVMBuildInsertValue(ctx->builder, undef, LLVMConstInt(LLVMInt1Type(), 1, 0), 0, "set_valid");
    
    // Cast if necessary (float to int etc, simplistic check here)
    // Assume semantic check ensured compatibility
    LLVMValueRef res_2 = LLVMBuildInsertValue(ctx->builder, res_1, val, 1, "set_val");
    
    LLVMBuildRet(ctx->builder, res_2);
    
    // Resume Block
    LLVMBasicBlockRef resume_bb = LLVMAppendBasicBlock(func, "resume");
    LLVMAddCase(ctx->current_switch_inst, LLVMConstInt(LLVMInt32Type(), next_state, 0), resume_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, resume_bb);
}

void codegen_for_in(CodegenCtx *ctx, ForInNode *node) {
    // 1. Evaluate Collection
    LLVMValueRef col = codegen_expr(ctx, node->collection);
    VarType col_type = codegen_calc_type(ctx, node->collection);
    
    // 2. Setup Loop
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "for_cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "for_body");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlock(func, "for_end");
    
    // Loop State Variables
    LLVMValueRef iter_ptr = NULL; // For arrays/strings
    LLVMValueRef flux_ctx = NULL; // For flux
    
    if (col_type.base == TYPE_STRING || (col_type.base == TYPE_CHAR && col_type.ptr_depth == 1)) {
        // String Iteration
        iter_ptr = LLVMBuildAlloca(ctx->builder, LLVMPointerType(LLVMInt8Type(), 0), "str_iter");
        LLVMBuildStore(ctx->builder, col, iter_ptr);
    } else if (col_type.base == TYPE_INT) {
        // Range Iteration 0..N
        iter_ptr = LLVMBuildAlloca(ctx->builder, LLVMInt64Type(), "range_i");
        LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt64Type(), 0, 0), iter_ptr);
    } else {
        // Flux Iteration
        // col is likely the FluxCtx* returned by the Init function
        flux_ctx = col;
    }
    
    LLVMBuildBr(ctx->builder, cond_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    
    LLVMValueRef current_val = NULL;
    LLVMValueRef condition = NULL;
    
    if (flux_ctx) {
        // Call Next(ctx)
        // Need to construct function name: implicit assumption needed or lookup
        // Hack: We try to find {FluxName}_next. 
        // Since we don't have the flux name easily here without analyzing 'collection' expression deeply,
        // we try to derive it from the LLVM Struct Name or Fallback
        
        char next_name[256];
        int found = 0;
        
        if (node->collection->type == NODE_CALL) {
             CallNode* cn = (CallNode*)node->collection;
             snprintf(next_name, 256, "%s_next", cn->name);
             found = 1;
        } else {
             // Try to deduce from LLVM Type Name
             // LLVMTypeOf(flux_ctx) -> FluxCtx_NAME*
             LLVMTypeRef ptr_t = LLVMTypeOf(flux_ctx);
             if (LLVMGetTypeKind(ptr_t) == LLVMPointerTypeKind) {
                 LLVMTypeRef el_t = LLVMGetElementType(ptr_t);
                 // If it was stored in i8*, we can't get it. But if it is fresh or typed:
                 if (LLVMGetTypeKind(el_t) == LLVMStructTypeKind) {
                     const char *sname = LLVMGetStructName(el_t);
                     if (sname && strncmp(sname, "FluxCtx_", 8) == 0) {
                         // Found it! sname is FluxCtx_gen
                         // We want gen_next
                         snprintf(next_name, 256, "%s_next", sname + 8);
                         found = 1;
                     }
                 }
             }
        }
        
        if (!found) {
             // Fallback: This usually happens if flux_ctx was cast to i8*.
             // In simple compiler, assuming flux vars don't mix, we might fail here.
             snprintf(next_name, 256, "UnknownFlux_next");
        }

        LLVMValueRef next_func = LLVMGetNamedFunction(ctx->module, next_name);
        if (!next_func) {
             codegen_error(ctx, (ASTNode*)node, "Could not find flux next function. (Note: iterating variables bound to fluxes requires type inference that may be lost in this version)");
        }
        
        // Bitcast flux_ctx to the expected argument type (FluxCtx*)
        // This handles cases where flux_ctx is i8*
        LLVMTypeRef func_t = LLVMGlobalGetValueType(next_func);
        LLVMTypeRef expected_ptr_t = LLVMTypeOf(LLVMGetParam(next_func, 0));
        
        if (LLVMTypeOf(flux_ctx) != expected_ptr_t) {
            flux_ctx = LLVMBuildBitCast(ctx->builder, flux_ctx, expected_ptr_t, "ctx_cast");
        }

        LLVMValueRef res = LLVMBuildCall2(ctx->builder, func_t, next_func, &flux_ctx, 1, "res");
        condition = LLVMBuildExtractValue(ctx->builder, res, 0, "is_valid");
        current_val = LLVMBuildExtractValue(ctx->builder, res, 1, "val");
    } 
    else if (col_type.base == TYPE_INT) {
        LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), iter_ptr, "idx");
        // Cast col to i64
        LLVMValueRef limit = LLVMBuildIntCast(ctx->builder, col, LLVMInt64Type(), "limit");
        condition = LLVMBuildICmp(ctx->builder, LLVMIntSLT, idx, limit, "chk");
        current_val = LLVMBuildIntCast(ctx->builder, idx, LLVMInt32Type(), "val");
    }
    else {
        // String
        LLVMValueRef p = LLVMBuildLoad2(ctx->builder, LLVMPointerType(LLVMInt8Type(), 0), iter_ptr, "p");
        LLVMValueRef c = LLVMBuildLoad2(ctx->builder, LLVMInt8Type(), p, "char");
        condition = LLVMBuildICmp(ctx->builder, LLVMIntNE, c, LLVMConstInt(LLVMInt8Type(), 0, 0), "chk");
        current_val = c;
    }
    
    LLVMBuildCondBr(ctx->builder, condition, body_bb, end_bb);
    
    // Body
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    
    // Bind Loop Var
    LLVMTypeRef var_type = get_llvm_type(ctx, node->iter_type);
    
    LLVMValueRef var_alloca = NULL;
    int is_new_var = 1;
    
    // Check if we are in flux and var already exists in context (lifted to struct)
    // Logic: If in flux, symbols are already in ctx->symbols via codegen_flux_def pre-pass.
    // So find_symbol should return the GEP.
    if (ctx->flux_ctx_val) {
        Symbol *existing = find_symbol(ctx, node->var_name);
        if (existing) {
            var_alloca = existing->value;
            is_new_var = 0;
        }
    }
    
    if (!var_alloca) {
        var_alloca = LLVMBuildAlloca(ctx->builder, var_type, node->var_name);
    }
    
    LLVMBuildStore(ctx->builder, current_val, var_alloca);
    
    Symbol *saved_syms = ctx->symbols;
    if (is_new_var) {
        add_symbol(ctx, node->var_name, var_alloca, var_type, node->iter_type, 0, 0);
    }
    
    push_loop_ctx(ctx, cond_bb, end_bb);
    codegen_node(ctx, node->body);
    pop_loop_ctx(ctx);
    
    // Step
    if (flux_ctx) {
        // Step happens in check (by calling next again)
    } else if (col_type.base == TYPE_INT) {
        LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), iter_ptr, "idx");
        LLVMValueRef nxt = LLVMBuildAdd(ctx->builder, idx, LLVMConstInt(LLVMInt64Type(), 1, 0), "inc");
        LLVMBuildStore(ctx->builder, nxt, iter_ptr);
    } else {
        LLVMValueRef p = LLVMBuildLoad2(ctx->builder, LLVMPointerType(LLVMInt8Type(), 0), iter_ptr, "p");
        LLVMValueRef nxt = LLVMBuildGEP2(ctx->builder, LLVMInt8Type(), p, (LLVMValueRef[]){LLVMConstInt(LLVMInt64Type(), 1, 0)}, 1, "inc");
        LLVMBuildStore(ctx->builder, nxt, iter_ptr);
    }
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, cond_bb);
    }
    
    ctx->symbols = saved_syms;
    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
}
