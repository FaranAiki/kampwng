#ifndef CODEGEN_H
#define CODEGEN_H

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include "parser.h"

// --- TYPES ---

// Symbol Table Entry
typedef struct Symbol {
  char *name;
  LLVMValueRef value; // Pointer to memory (alloca or global)
  LLVMTypeRef type;   // The LLVM Type
  int is_array;
  int is_mutable;
  struct Symbol *next;
} Symbol;

// Context
typedef struct {
  LLVMModuleRef module;
  LLVMBuilderRef builder;
  LLVMValueRef printf_func;
  LLVMTypeRef printf_type;
  LLVMValueRef input_func; 
  LLVMValueRef strcmp_func; 
  Symbol *symbols; 
} CodegenCtx;

// --- PROTOTYPES ---

// Standard compiler entry point
LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name);

// Internal functions exposed for CLI/JIT
void codegen_init_ctx(CodegenCtx *ctx, LLVMModuleRef module, LLVMBuilderRef builder);
void add_symbol(CodegenCtx *ctx, const char *name, LLVMValueRef val, LLVMTypeRef type, int is_array, int is_mut);
Symbol* find_symbol(CodegenCtx *ctx, const char *name);
LLVMTypeRef get_llvm_type(VarType t);

// Granular codegen functions
LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node);
void codegen_node(CodegenCtx *ctx, ASTNode *node);
void codegen_func_def(CodegenCtx *ctx, FuncDefNode *node);
void codegen_var_decl(CodegenCtx *ctx, VarDeclNode *node);

#endif
