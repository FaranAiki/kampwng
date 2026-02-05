#include "codegen.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations of generators
LLVMValueRef generate_strlen(LLVMModuleRef module);
LLVMValueRef generate_strcpy(LLVMModuleRef module);
LLVMValueRef generate_strdup(LLVMModuleRef module, LLVMValueRef malloc_func, LLVMValueRef strlen_func, LLVMValueRef strcpy_func);
LLVMValueRef generate_input_func(LLVMModuleRef module, LLVMBuilderRef builder, LLVMValueRef malloc_func, LLVMValueRef getchar_func);

void codegen_init_ctx(CodegenCtx *ctx, LLVMModuleRef module, LLVMBuilderRef builder) {
    ctx->module = module;
    ctx->builder = builder;
    ctx->symbols = NULL;
    ctx->functions = NULL;
    ctx->classes = NULL;
    ctx->current_loop = NULL;

    // Printf
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->printf_type = LLVMFunctionType(LLVMInt32Type(), printf_args, 1, true);
    ctx->printf_func = LLVMAddFunction(module, "printf", ctx->printf_type);
    
    // Malloc
    LLVMTypeRef malloc_args[] = { LLVMInt64Type() };
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), malloc_args, 1, false);
    ctx->malloc_func = LLVMAddFunction(module, "malloc", malloc_type);
    
    // Getchar
    LLVMTypeRef getchar_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
    LLVMValueRef getchar_func = LLVMAddFunction(module, "getchar", getchar_type);
    
    // Strcmp
    LLVMTypeRef strcmp_args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32Type(), strcmp_args, 2, false);
    ctx->strcmp_func = LLVMAddFunction(module, "strcmp", strcmp_type);

    // String Helpers
    ctx->strlen_func = generate_strlen(module);
    ctx->strcpy_func = generate_strcpy(module);
    ctx->strdup_func = generate_strdup(module, ctx->malloc_func, ctx->strlen_func, ctx->strcpy_func);

    // Input
    ctx->input_func = generate_input_func(module, builder, ctx->malloc_func, getchar_func);
}

// ... Symbol management functions ...
void add_symbol(CodegenCtx *ctx, const char *name, LLVMValueRef val, LLVMTypeRef type, VarType vtype, int is_array, int is_mut) {
  Symbol *s = malloc(sizeof(Symbol));
  s->name = strdup(name);
  s->value = val;
  s->type = type;
  s->vtype = vtype;
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

void add_func_symbol(CodegenCtx *ctx, const char *name, VarType ret_type) {
    FuncSymbol *s = malloc(sizeof(FuncSymbol));
    s->name = strdup(name);
    s->ret_type = ret_type;
    s->next = ctx->functions;
    ctx->functions = s;
}

FuncSymbol* find_func_symbol(CodegenCtx *ctx, const char *name) {
    FuncSymbol *curr = ctx->functions;
    while(curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

void add_class_info(CodegenCtx *ctx, ClassInfo *ci) {
    ci->next = ctx->classes;
    ctx->classes = ci;
}

ClassInfo* find_class(CodegenCtx *ctx, const char *name) {
    ClassInfo *cur = ctx->classes;
    while(cur) {
        if (strcmp(cur->name, name) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

int get_member_index(ClassInfo *ci, const char *member, LLVMTypeRef *out_type, VarType *out_vtype) {
    ClassMember *m = ci->members;
    while(m) {
        if (strcmp(m->name, member) == 0) {
            if (out_type) *out_type = m->type;
            if (out_vtype) *out_vtype = m->vtype;
            return m->index;
        }
        m = m->next;
    }
    return -1;
}

LLVMTypeRef get_llvm_type(CodegenCtx *ctx, VarType t) {
  LLVMTypeRef base_type;
  switch (t.base) {
    case TYPE_INT: base_type = LLVMInt32Type(); break;
    case TYPE_CHAR: base_type = LLVMInt8Type(); break;
    case TYPE_BOOL: base_type = LLVMInt1Type(); break;
    case TYPE_FLOAT: base_type = LLVMFloatType(); break;
    case TYPE_DOUBLE: base_type = LLVMDoubleType(); break;
    case TYPE_VOID: base_type = LLVMVoidType(); break;
    case TYPE_STRING: base_type = LLVMPointerType(LLVMInt8Type(), 0); break;
    case TYPE_CLASS: {
        if (!t.class_name) return LLVMInt32Type(); 
        ClassInfo *ci = find_class(ctx, t.class_name);
        if (ci) base_type = ci->struct_type;
        else base_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), t.class_name);
        break;
    }
    default: base_type = LLVMInt32Type(); break;
  }
  for (int i=0; i<t.ptr_depth; i++) base_type = LLVMPointerType(base_type, 0);
  
  // FIX: Handle fixed-size arrays in type system
  if (t.array_size > 0) {
      base_type = LLVMArrayType(base_type, t.array_size);
  }
  
  return base_type;
}

// --- Helpers ---

ClassMember** append_member(CodegenCtx *ctx, ClassInfo *ci, ClassMember **tail, int *idx, const char *name, VarType vt, LLVMTypeRef lt, ASTNode *init) {
    ClassMember *cm = malloc(sizeof(ClassMember));
    cm->name = strdup(name);
    cm->vtype = vt;
    cm->type = lt;
    cm->index = (*idx)++;
    cm->init_expr = init; 
    cm->next = NULL;
    *tail = cm;
    return &cm->next;
}

// --- Internal String Functions ---

LLVMValueRef generate_strlen(LLVMModuleRef module) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef type = LLVMFunctionType(LLVMInt64Type(), args, 1, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strlen", type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlock(func, "loop");
    LLVMBasicBlockRef end = LLVMAppendBasicBlock(func, "end");
    
    LLVMBuilderRef b = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(b, entry);
    
    LLVMValueRef str = LLVMGetParam(func, 0);
    LLVMValueRef i_ptr = LLVMBuildAlloca(b, LLVMInt64Type(), "i");
    LLVMBuildStore(b, LLVMConstInt(LLVMInt64Type(), 0, 0), i_ptr);
    LLVMBuildBr(b, loop);
    
    LLVMPositionBuilderAtEnd(b, loop);
    LLVMValueRef i = LLVMBuildLoad2(b, LLVMInt64Type(), i_ptr, "i_val");
    LLVMValueRef char_ptr = LLVMBuildGEP2(b, LLVMInt8Type(), str, &i, 1, "char_ptr");
    LLVMValueRef c = LLVMBuildLoad2(b, LLVMInt8Type(), char_ptr, "c");
    LLVMValueRef is_null = LLVMBuildICmp(b, LLVMIntEQ, c, LLVMConstInt(LLVMInt8Type(), 0, 0), "is_null");
    
    LLVMBasicBlockRef step = LLVMAppendBasicBlock(func, "step");
    LLVMBuildCondBr(b, is_null, end, step);
    
    LLVMPositionBuilderAtEnd(b, step);
    LLVMValueRef next_i = LLVMBuildAdd(b, i, LLVMConstInt(LLVMInt64Type(), 1, 0), "next_i");
    LLVMBuildStore(b, next_i, i_ptr);
    LLVMBuildBr(b, loop);
    
    LLVMPositionBuilderAtEnd(b, end);
    LLVMBuildRet(b, i);
    LLVMDisposeBuilder(b);
    return func;
}

LLVMValueRef generate_strcpy(LLVMModuleRef module) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), args, 2, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strcpy", type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlock(func, "loop");
    LLVMBasicBlockRef end = LLVMAppendBasicBlock(func, "end");
    
    LLVMBuilderRef b = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(b, entry);
    
    LLVMValueRef dest = LLVMGetParam(func, 0);
    LLVMValueRef src = LLVMGetParam(func, 1);
    LLVMValueRef i_ptr = LLVMBuildAlloca(b, LLVMInt64Type(), "i");
    LLVMBuildStore(b, LLVMConstInt(LLVMInt64Type(), 0, 0), i_ptr);
    LLVMBuildBr(b, loop);
    
    LLVMPositionBuilderAtEnd(b, loop);
    LLVMValueRef i = LLVMBuildLoad2(b, LLVMInt64Type(), i_ptr, "i_val");
    LLVMValueRef src_ptr = LLVMBuildGEP2(b, LLVMInt8Type(), src, &i, 1, "src_ptr");
    LLVMValueRef c = LLVMBuildLoad2(b, LLVMInt8Type(), src_ptr, "c");
    LLVMValueRef dest_ptr = LLVMBuildGEP2(b, LLVMInt8Type(), dest, &i, 1, "dest_ptr");
    LLVMBuildStore(b, c, dest_ptr);
    
    LLVMValueRef is_null = LLVMBuildICmp(b, LLVMIntEQ, c, LLVMConstInt(LLVMInt8Type(), 0, 0), "is_null");
    
    LLVMBasicBlockRef step = LLVMAppendBasicBlock(func, "step");
    LLVMBuildCondBr(b, is_null, end, step);
    
    LLVMPositionBuilderAtEnd(b, step);
    LLVMValueRef next_i = LLVMBuildAdd(b, i, LLVMConstInt(LLVMInt64Type(), 1, 0), "next_i");
    LLVMBuildStore(b, next_i, i_ptr);
    LLVMBuildBr(b, loop);
    
    LLVMPositionBuilderAtEnd(b, end);
    LLVMBuildRet(b, dest);
    LLVMDisposeBuilder(b);
    return func;
}

LLVMValueRef generate_strdup(LLVMModuleRef module, LLVMValueRef malloc_func, LLVMValueRef strlen_func, LLVMValueRef strcpy_func) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), args, 1, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strdup", type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBuilderRef b = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(b, entry);
    
    LLVMValueRef str = LLVMGetParam(func, 0);
    LLVMValueRef len = LLVMBuildCall2(b, LLVMGlobalGetValueType(strlen_func), strlen_func, &str, 1, "len");
    LLVMValueRef len_plus_1 = LLVMBuildAdd(b, len, LLVMConstInt(LLVMInt64Type(), 1, 0), "len_plus_1");
    LLVMValueRef mem = LLVMBuildCall2(b, LLVMGlobalGetValueType(malloc_func), malloc_func, &len_plus_1, 1, "mem");
    
    LLVMValueRef copy_args[] = { mem, str };
    LLVMBuildCall2(b, LLVMGlobalGetValueType(strcpy_func), strcpy_func, copy_args, 2, "copy_res");
    
    LLVMBuildRet(b, mem);
    LLVMDisposeBuilder(b);
    return func;
}

// ... (Rest of input func generator remains same) ...
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
  
  // 1. Classes
  ASTNode *iter = root;
  while(iter) {
      if (iter->type == NODE_CLASS) {
          ClassNode *cn = (ClassNode*)iter;
          ClassInfo *ci = malloc(sizeof(ClassInfo));
          ci->name = strdup(cn->name);
          ci->parent_name = cn->parent_name ? strdup(cn->parent_name) : NULL;
          ci->struct_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), cn->name);
          ci->members = NULL;
          add_class_info(&ctx, ci);
      }
      iter = iter->next;
  }
  
  // 1.5 Class Bodies
  iter = root;
  while(iter) {
      if (iter->type == NODE_CLASS) {
          ClassNode *cn = (ClassNode*)iter;
          ClassInfo *ci = find_class(&ctx, cn->name);
          
          ci->members = NULL;
          ClassMember **tail = &ci->members;
          int idx = 0;
          int member_count = 0;

          if (ci->parent_name) {
              ClassInfo *parent = find_class(&ctx, ci->parent_name);
              if (parent) {
                  ClassMember *pm = parent->members;
                  while(pm) {
                      tail = append_member(&ctx, ci, tail, &idx, pm->name, pm->vtype, pm->type, pm->init_expr);
                      member_count++;
                      pm = pm->next;
                  }
              }
          }
          
          ASTNode *m = cn->members;
          while(m) {
              if (m->type == NODE_VAR_DECL) {
                  VarDeclNode *vd = (VarDeclNode*)m;
                  
                  // Fix: Properly handle array size in class members
                  if (vd->is_array) {
                       int size = 1; 
                       // Check explicit size literal
                       if (vd->array_size && vd->array_size->type == NODE_LITERAL) {
                           size = ((LiteralNode*)vd->array_size)->val.int_val;
                       } 
                       // Check implicit string size
                       else if (vd->initializer && vd->initializer->type == NODE_LITERAL) {
                           LiteralNode *lit = (LiteralNode*)vd->initializer;
                           if (lit->var_type.base == TYPE_STRING) {
                                size = strlen(lit->val.str_val) + 1;
                           }
                       }
                       // Store size in VarType so get_llvm_type returns ArrayType
                       vd->var_type.array_size = size;
                  }

                  tail = append_member(&ctx, ci, tail, &idx, vd->name, vd->var_type, get_llvm_type(&ctx, vd->var_type), vd->initializer);
                  member_count++;
              }
              m = m->next;
          }
          
          if (cn->traits.names) {
              for(int i=0; i<cn->traits.count; i++) {
                  char *tname = cn->traits.names[i];
                  ClassInfo *ti = find_class(&ctx, tname);
                  if (ti) {
                      char member_name[128];
                      sprintf(member_name, "__trait_%s", tname);
                      VarType vt = {TYPE_CLASS, 0, strdup(tname), 0};
                      tail = append_member(&ctx, ci, tail, &idx, member_name, vt, ti->struct_type, NULL);
                      member_count++;
                  }
              }
          }
          
          LLVMTypeRef *elem_types = malloc(sizeof(LLVMTypeRef) * member_count);
          ClassMember *cur = ci->members;
          for(int i=0; i<member_count; i++) {
              elem_types[i] = cur->type;
              cur = cur->next;
          }
          LLVMStructSetBody(ci->struct_type, elem_types, member_count, false);
          free(elem_types);
      }
      iter = iter->next;
  }

  // 2. Functions
  iter = root;
  while(iter) {
      if (iter->type == NODE_FUNC_DEF) {
          FuncDefNode *fd = (FuncDefNode*)iter;
          add_func_symbol(&ctx, fd->name, fd->ret_type);
      }
      if (iter->type == NODE_CLASS) {
          ClassNode *cn = (ClassNode*)iter;
          ASTNode *m = cn->members;
          while(m) {
              if (m->type == NODE_FUNC_DEF) {
                  FuncDefNode *fd = (FuncDefNode*)m;
                  char mangled[256];
                  sprintf(mangled, "%s_%s", cn->name, fd->name);
                  add_func_symbol(&ctx, mangled, fd->ret_type);
              }
              m = m->next;
          }
      }
      iter = iter->next;
  }

  // 3. Code Gen
  ASTNode *curr = root;
  while (curr) {
    if (curr->type == NODE_FUNC_DEF) {
      codegen_func_def(&ctx, (FuncDefNode*)curr);
    }
    if (curr->type == NODE_CLASS) {
        ClassNode *cn = (ClassNode*)curr;
        ASTNode *m = cn->members;
        while(m) {
            if (m->type == NODE_FUNC_DEF) {
                FuncDefNode *fd = (FuncDefNode*)m;
                char *original_name = fd->name;
                char mangled[256];
                sprintf(mangled, "%s_%s", cn->name, fd->name);
                fd->name = mangled;
                fd->class_name = cn->name;
                codegen_func_def(&ctx, fd);
                fd->name = original_name; 
            }
            m = m->next;
        }
    }
    curr = curr->next;
  }

  // Main
  int has_stmts = 0;
  curr = root;
  while(curr) {
    if (curr->type != NODE_FUNC_DEF && curr->type != NODE_LINK && curr->type != NODE_CLASS) { has_stmts = 1; break; }
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
          if (curr->type != NODE_FUNC_DEF && curr->type != NODE_LINK && curr->type != NODE_CLASS) {
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
  FuncSymbol *f = ctx.functions;
  while(f) { FuncSymbol *next = f->next; free(f->name); free(f); f = next; }
  ClassInfo *c = ctx.classes;
  while(c) {
      ClassMember *cm = c->members;
      while(cm) { ClassMember *nxt = cm->next; free(cm->name); free(cm); cm = nxt; }
      ClassInfo *nxt = c->next;
      free(c->name); if(c->parent_name) free(c->parent_name); free(c);
      c = nxt;
  }

  return module;
}
