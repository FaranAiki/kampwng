#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Structs are now in codegen.h

void codegen_init_ctx(CodegenCtx *ctx, LLVMModuleRef module, LLVMBuilderRef builder) {
    ctx->module = module;
    ctx->builder = builder;
    ctx->symbols = NULL;

    // Setup Built-ins
    // printf
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->printf_type = LLVMFunctionType(LLVMInt32Type(), printf_args, 1, true);
    ctx->printf_func = LLVMAddFunction(module, "printf", ctx->printf_type);
    
    // malloc (for input)
    LLVMTypeRef malloc_args[] = { LLVMInt64Type() };
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), malloc_args, 1, false);
    LLVMValueRef malloc_func = LLVMAddFunction(module, "malloc", malloc_type);
    
    // getchar (for input)
    LLVMTypeRef getchar_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
    LLVMValueRef getchar_func = LLVMAddFunction(module, "getchar", getchar_type);
    
    // strcmp
    LLVMTypeRef strcmp_args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32Type(), strcmp_args, 2, false);
    ctx->strcmp_func = LLVMAddFunction(module, "strcmp", strcmp_type);

    // Helper to generate the input function body
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

char* format_string(const char* input) {
  if (!input) return NULL;
  size_t len = strlen(input);
  char *new_str = malloc(len + 2);
  strcpy(new_str, input);
  return new_str;
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

// Forward Decl
LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node);
void codegen_node(CodegenCtx *ctx, ASTNode *node);

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
    if (!sym) { fprintf(stderr, "Error: Undefined variable %s\n", r->name); return LLVMConstInt(LLVMInt32Type(), 0, 0); }
    
    if (sym->is_array) {
        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
        return LLVMBuildGEP2(ctx->builder, sym->type, sym->value, indices, 2, "array_decay");
    }
    
    return LLVMBuildLoad2(ctx->builder, sym->type, sym->value, r->name);
  }
  else if (node->type == NODE_ARRAY_ACCESS) {
    ArrayAccessNode *an = (ArrayAccessNode*)node;
    Symbol *sym = find_symbol(ctx, an->name);
    if (!sym) { fprintf(stderr, "Error: Undefined variable %s\n", an->name); exit(1); }
    if (!sym->is_array) { fprintf(stderr, "Error: %s is not an array\n", an->name); exit(1); }

    LLVMValueRef idx = codegen_expr(ctx, an->index);
    if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
        idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
    } else {
        idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
    }

    LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), idx };
    LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, sym->type, sym->value, indices, 2, "elem_ptr");
    
    LLVMTypeRef elem_type = LLVMGetElementType(sym->type);
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
        case TOKEN_XOR: return LLVMBuildXor(ctx->builder, l, r, "xor");
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

void codegen_assign(CodegenCtx *ctx, AssignNode *node) {
  Symbol *sym = find_symbol(ctx, node->name);
  if (!sym) { fprintf(stderr, "Error: Assignment to undefined variable %s\n", node->name); exit(1); }
  
  if (!sym->is_mutable) {
      fprintf(stderr, "Error: Assignment to immutable variable %s\n", node->name);
      exit(1);
  }

  LLVMValueRef val = codegen_expr(ctx, node->value);

  if (node->index) {
      // Array assignment: name[i] = val
      if (!sym->is_array) { fprintf(stderr, "Error: Indexing non-array %s\n", node->name); exit(1); }
      
      LLVMValueRef idx = codegen_expr(ctx, node->index);
      if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
         idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
      } else {
         idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
      }

      LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), idx };
      LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, sym->type, sym->value, indices, 2, "elem_ptr");
      LLVMBuildStore(ctx->builder, val, ptr);

  } else {
      LLVMBuildStore(ctx->builder, val, sym->value);
  }
}

void codegen_var_decl(CodegenCtx *ctx, VarDeclNode *node) {
  LLVMValueRef alloca = NULL;
  LLVMTypeRef type = NULL;

  if (node->is_array) {
      LLVMTypeRef elem_type = (node->var_type == VAR_AUTO) ? LLVMInt32Type() : get_llvm_type(node->var_type);
      
      int size = 0;
      if (node->array_size) {
          if (node->array_size->type == NODE_LITERAL) {
             size = ((LiteralNode*)node->array_size)->val.int_val;
          } else {
              size = 10; 
          }
      } else {
          if (node->initializer && node->initializer->type == NODE_ARRAY_LIT) {
             ASTNode *el = ((ArrayLitNode*)node->initializer)->elements;
             while(el) { size++; el = el->next; }
          } else if (node->initializer && node->initializer->type == NODE_LITERAL && ((LiteralNode*)node->initializer)->var_type == VAR_STRING) {
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
          } else if (node->initializer->type == NODE_LITERAL && ((LiteralNode*)node->initializer)->var_type == VAR_STRING) {
             char *str = ((LiteralNode*)node->initializer)->val.str_val;
             for (int i = 0; i < size; i++) {
                 LLVMValueRef val = LLVMConstInt(LLVMInt8Type(), str[i], 0); 
                 LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), i, 0) };
                 LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, type, alloca, indices, 2, "str_init_ptr");
                 LLVMBuildStore(ctx->builder, val, ptr);
             }
          }
      }
      
  } else {
      LLVMValueRef init_val = codegen_expr(ctx, node->initializer);
      
      if (node->var_type == VAR_AUTO) {
        type = LLVMTypeOf(init_val);
      } else {
        type = get_llvm_type(node->var_type);
      }
      
      alloca = LLVMBuildAlloca(ctx->builder, type, node->name);
      LLVMBuildStore(ctx->builder, init_val, alloca);
  }

  add_symbol(ctx, node->name, alloca, type, node->is_array, node->is_mutable);
}

void codegen_func_def(CodegenCtx *ctx, FuncDefNode *node) {
  int param_count = 0;
  Parameter *p = node->params;
  while(p) { param_count++; p = p->next; }
  
  LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * param_count);
  p = node->params;
  for(int i=0; i<param_count; i++) {
    param_types[i] = get_llvm_type(p->type);
    p = p->next;
  }
  
  LLVMTypeRef ret_type = get_llvm_type(node->ret_type);
  LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types, param_count, node->is_varargs); // HANDLE VARARGS
  LLVMValueRef func = LLVMAddFunction(ctx->module, node->name, func_type);
  free(param_types);
  
  // IF EXTERN (FFI), DO NOT GENERATE BODY
  if (!node->body) {
      return; 
  }

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
  LLVMBasicBlockRef prev_block = LLVMGetInsertBlock(ctx->builder); 
  LLVMPositionBuilderAtEnd(ctx->builder, entry);
  
  Symbol *saved_scope = ctx->symbols;
  
  p = node->params;
  for(int i=0; i<param_count; i++) {
    LLVMValueRef arg_val = LLVMGetParam(func, i);
    LLVMTypeRef type = get_llvm_type(p->type);
    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, type, p->name);
    LLVMBuildStore(ctx->builder, arg_val, alloca);
    add_symbol(ctx, p->name, alloca, type, 0, 1); 
    p = p->next;
  }
  
  codegen_node(ctx, node->body);
  
  if (node->ret_type == VAR_VOID) {
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

void codegen_return(CodegenCtx *ctx, ReturnNode *node) {
  if (node->value) {
    LLVMValueRef ret = codegen_expr(ctx, node->value);
    LLVMBuildRet(ctx->builder, ret);
  } else {
    LLVMBuildRetVoid(ctx->builder);
  }
}

void codegen_loop(CodegenCtx *ctx, LoopNode *node) {
  LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
  LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "loop_cond");
  LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "loop_body");
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
  codegen_node(ctx, node->body);
  
  LLVMValueRef cur_i_body = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), counter_ptr, "i_val_body");
  LLVMValueRef next_i = LLVMBuildAdd(ctx->builder, cur_i_body, LLVMConstInt(LLVMInt64Type(), 1, 0), "next_i");
  LLVMBuildStore(ctx->builder, next_i, counter_ptr);
  LLVMBuildBr(ctx->builder, cond_bb);

  LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
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

void codegen_node(CodegenCtx *ctx, ASTNode *node) {
  while (node) {
    if (node->type == NODE_FUNC_DEF) codegen_func_def(ctx, (FuncDefNode*)node);
    else if (node->type == NODE_RETURN) codegen_return(ctx, (ReturnNode*)node);
    else if (node->type == NODE_CALL) codegen_expr(ctx, node); 
    else if (node->type == NODE_LOOP) codegen_loop(ctx, (LoopNode*)node);
    else if (node->type == NODE_IF) codegen_if(ctx, (IfNode*)node);
    else if (node->type == NODE_VAR_DECL) codegen_var_decl(ctx, (VarDeclNode*)node);
    else if (node->type == NODE_ASSIGN) codegen_assign(ctx, (AssignNode*)node);
    else if (node->type == NODE_ARRAY_ACCESS) codegen_expr(ctx, node); 
    node = node->next;
  }
}

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
    if (curr->type != NODE_FUNC_DEF) { has_stmts = 1; break; }
    curr = curr->next;
  }

  if (has_stmts) {
    LLVMTypeRef main_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
    LLVMValueRef main_func = LLVMAddFunction(module, "main", main_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);
    
    curr = root;
    while (curr) {
      if (curr->type != NODE_FUNC_DEF) {
        ASTNode *next = curr->next;
        curr->next = NULL; 
        codegen_node(&ctx, curr);
        curr->next = next; 
      }
      curr = curr->next;
    }
    
    LLVMBuildRet(builder, LLVMConstInt(LLVMInt32Type(), 0, 0));
  }

  LLVMDisposeBuilder(builder);
  
  Symbol *s = ctx.symbols;
  while(s) { Symbol *next = s->next; free(s->name); free(s); s = next; }

  return module;
}
