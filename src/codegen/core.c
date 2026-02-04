#include "codegen.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void codegen_init_ctx(CodegenCtx *ctx, LLVMModuleRef module, LLVMBuilderRef builder) {
    ctx->module = module;
    ctx->builder = builder;
    ctx->symbols = NULL;
    ctx->current_loop = NULL;

    // printf
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->printf_type = LLVMFunctionType(LLVMInt32Type(), printf_args, 1, true);
    ctx->printf_func = LLVMAddFunction(module, "printf", ctx->printf_type);
    
    // malloc
    LLVMTypeRef malloc_args[] = { LLVMInt64Type() };
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), malloc_args, 1, false);
    LLVMValueRef malloc_func = LLVMAddFunction(module, "malloc", malloc_type);
    
    // getchar
    LLVMTypeRef getchar_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
    LLVMValueRef getchar_func = LLVMAddFunction(module, "getchar", getchar_type);
    
    // strcmp
    LLVMTypeRef strcmp_args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32Type(), strcmp_args, 2, false);
    ctx->strcmp_func = LLVMAddFunction(module, "strcmp", strcmp_type);

    // Input func helper
    LLVMValueRef generate_input_func(LLVMModuleRef module, LLVMBuilderRef builder, LLVMValueRef malloc_func, LLVMValueRef getchar_func);
    ctx->input_func = generate_input_func(module, builder, malloc_func, getchar_func);
}

void add_symbol(CodegenCtx *ctx, const char *name, LLVMValueRef val, LLVMTypeRef type, int is_array, int is_mut) {
  Symbol *s = malloc(sizeof(Symbol));
  s->name = strdup(name);
  s->value = val;
  s->type = type;
  s->is_array = is_array;
  s->is_mutable = is_mut;
  s->next = ctx->symbols;
  ctx->symbols = s;
}

Symbol* find_symbol(CodegenCtx *ctx, const char *name) {
  Symbol *curr = ctx->symbols;
  while (curr) {
    if (strcmp(curr->name, name) == 0) return curr;
    curr = curr->next;
  }
  return NULL;
}

LLVMTypeRef get_llvm_type(VarType t) {
  switch (t) {
    case VAR_INT: return LLVMInt32Type();
    case VAR_CHAR: return LLVMInt8Type();
    case VAR_BOOL: return LLVMInt1Type();
    case VAR_FLOAT: return LLVMFloatType();
    case VAR_DOUBLE: return LLVMDoubleType();
    case VAR_VOID: return LLVMVoidType();
    case VAR_STRING: return LLVMPointerType(LLVMInt8Type(), 0);
    default: return LLVMInt32Type();
  }
}

// Helper to generate the input function body
LLVMValueRef generate_input_func(LLVMModuleRef module, LLVMBuilderRef builder, LLVMValueRef malloc_func, LLVMValueRef getchar_func) {
    LLVMTypeRef ret_type = LLVMPointerType(LLVMInt8Type(), 0);
    LLVMTypeRef func_type = LLVMFunctionType(ret_type, NULL, 0, false);
    LLVMValueRef func = LLVMAddFunction(module, "input", func_type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef loop_cond = LLVMAppendBasicBlock(func, "loop_cond");
    LLVMBasicBlockRef loop_body = LLVMAppendBasicBlock(func, "loop_body");
    LLVMBasicBlockRef loop_end = LLVMAppendBasicBlock(func, "loop_end");

    LLVMBuilderRef b = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(b, entry);

    LLVMValueRef buf_size = LLVMConstInt(LLVMInt64Type(), 256, 0);
    LLVMValueRef buf_args[] = { buf_size };
    LLVMValueRef buf = LLVMBuildCall2(b, LLVMGlobalGetValueType(malloc_func), malloc_func, buf_args, 1, "buf");

    LLVMValueRef i_ptr = LLVMBuildAlloca(b, LLVMInt32Type(), "i");
    LLVMBuildStore(b, LLVMConstInt(LLVMInt32Type(), 0, 0), i_ptr);
    
    LLVMBuildBr(b, loop_cond);

    LLVMPositionBuilderAtEnd(b, loop_cond);
    LLVMValueRef c = LLVMBuildCall2(b, LLVMGlobalGetValueType(getchar_func), getchar_func, NULL, 0, "c");
    LLVMValueRef is_nl = LLVMBuildICmp(b, LLVMIntEQ, c, LLVMConstInt(LLVMInt32Type(), 10, 0), "is_nl");
    LLVMValueRef is_eof = LLVMBuildICmp(b, LLVMIntEQ, c, LLVMConstInt(LLVMInt32Type(), -1, 0), "is_eof");
    LLVMValueRef stop = LLVMBuildOr(b, is_nl, is_eof, "stop");
    
    LLVMValueRef curr_i = LLVMBuildLoad2(b, LLVMInt32Type(), i_ptr, "curr_i");
    LLVMValueRef max_len = LLVMConstInt(LLVMInt32Type(), 255, 0);
    LLVMValueRef is_full = LLVMBuildICmp(b, LLVMIntSGE, curr_i, max_len, "is_full");
    
    LLVMValueRef stop_final = LLVMBuildOr(b, stop, is_full, "stop_final");
    
    LLVMBuildCondBr(b, stop_final, loop_end, loop_body);

    LLVMPositionBuilderAtEnd(b, loop_body);
    LLVMValueRef char_trunc = LLVMBuildTrunc(b, c, LLVMInt8Type(), "char");
    LLVMValueRef ptr = LLVMBuildGEP2(b, LLVMInt8Type(), buf, &curr_i, 1, "ptr");
    LLVMBuildStore(b, char_trunc, ptr);
    
    LLVMValueRef next_i = LLVMBuildAdd(b, curr_i, LLVMConstInt(LLVMInt32Type(), 1, 0), "next_i");
    LLVMBuildStore(b, next_i, i_ptr);
    LLVMBuildBr(b, loop_cond);

    LLVMPositionBuilderAtEnd(b, loop_end);
    curr_i = LLVMBuildLoad2(b, LLVMInt32Type(), i_ptr, "final_i");
    LLVMValueRef end_ptr = LLVMBuildGEP2(b, LLVMInt8Type(), buf, &curr_i, 1, "end_ptr");
    LLVMBuildStore(b, LLVMConstInt(LLVMInt8Type(), 0, 0), end_ptr);
    
    LLVMBuildRet(b, buf);
    LLVMDisposeBuilder(b);

    return func;
}

LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name) {
  LLVMModuleRef module = LLVMModuleCreateWithName(module_name);
  LLVMBuilderRef builder = LLVMCreateBuilder();

  CodegenCtx ctx;
  codegen_init_ctx(&ctx, module, builder);

  // 1. Generate Explicit Functions First
  ASTNode *curr = root;
  while (curr) {
    if (curr->type == NODE_FUNC_DEF) {
      codegen_func_def(&ctx, (FuncDefNode*)curr);
    }
    curr = curr->next;
  }

  // 2. Generate Implicit Main
  int has_stmts = 0;
  curr = root;
  while(curr) {
    if (curr->type != NODE_FUNC_DEF && curr->type != NODE_LINK) { has_stmts = 1; break; }
    curr = curr->next;
  }

  LLVMValueRef explicit_main = LLVMGetNamedFunction(module, "main");

  if (has_stmts) {
    if (explicit_main) {
        fprintf(stderr, "Warning: Top-level statements are ignored because an explicit 'main' function is defined.\n");
    } else {
        LLVMTypeRef main_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
        LLVMValueRef main_func = LLVMAddFunction(module, "main", main_type);
        LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");
        LLVMPositionBuilderAtEnd(builder, entry);
        
        curr = root;
        while (curr) {
          if (curr->type != NODE_FUNC_DEF && curr->type != NODE_LINK) {
            ASTNode *next = curr->next;
            curr->next = NULL; 
            codegen_node(&ctx, curr);
            curr->next = next; 
          }
          curr = curr->next;
        }
        
        LLVMBuildRet(builder, LLVMConstInt(LLVMInt32Type(), 0, 0));
    }
  }

  LLVMDisposeBuilder(builder);
  
  Symbol *s = ctx.symbols;
  while(s) { Symbol *next = s->next; free(s->name); free(s); s = next; }

  return module;
}
