#ifndef CODEGEN_H
#define CODEGEN_H

#include "parser.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Analysis.h>

typedef struct Symbol {
  char *name;
  LLVMValueRef value;
  LLVMTypeRef type; 
  VarType vtype;    
  int is_array;
  int is_mutable;
  struct Symbol *next;
} Symbol;

typedef struct FuncSymbol {
    char *name;
    VarType ret_type;
    struct FuncSymbol *next;
} FuncSymbol;

typedef struct ClassMember {
    char *name;
    LLVMTypeRef type;
    int index;
    VarType vtype;
    ASTNode *init_expr; 
    struct ClassMember *next;
} ClassMember;

typedef struct ClassInfo {
    char *name;
    char *parent_name; // Inheritance
    LLVMTypeRef struct_type;
    ClassMember *members;
    struct ClassInfo *next;
} ClassInfo;

typedef struct LoopContext {
  LLVMBasicBlockRef continue_target;
  LLVMBasicBlockRef break_target;
  struct LoopContext *parent;
} LoopContext;

typedef struct {
  LLVMModuleRef module;
  LLVMBuilderRef builder;
  Symbol *symbols;
  FuncSymbol *functions;
  ClassInfo *classes; 
  LoopContext *current_loop; 
  
  LLVMTypeRef printf_type;
  LLVMValueRef printf_func;
  
  LLVMValueRef input_func;
  LLVMValueRef strcmp_func;
} CodegenCtx;

// --- Core API ---
void codegen_init_ctx(CodegenCtx *ctx, LLVMModuleRef module, LLVMBuilderRef builder);
LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name);

// --- Shared Internal ---
void add_symbol(CodegenCtx *ctx, const char *name, LLVMValueRef val, LLVMTypeRef type, VarType vtype, int is_array, int is_mut);
Symbol* find_symbol(CodegenCtx *ctx, const char *name);
void add_func_symbol(CodegenCtx *ctx, const char *name, VarType ret_type);
FuncSymbol* find_func_symbol(CodegenCtx *ctx, const char *name);

// Class Helpers
void add_class_info(CodegenCtx *ctx, ClassInfo *ci);
ClassInfo* find_class(CodegenCtx *ctx, const char *name);
int get_member_index(ClassInfo *ci, const char *member, LLVMTypeRef *out_type, VarType *out_vtype);

LLVMTypeRef get_llvm_type(CodegenCtx *ctx, VarType t); 
VarType codegen_calc_type(CodegenCtx *ctx, ASTNode *node);
LLVMValueRef codegen_addr(CodegenCtx *ctx, ASTNode *node);

// --- Dispatchers ---
LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node);
void codegen_node(CodegenCtx *ctx, ASTNode *node);

// --- Stmt Handlers ---
void codegen_assign(CodegenCtx *ctx, AssignNode *node);
void codegen_var_decl(CodegenCtx *ctx, VarDeclNode *node);
void codegen_return(CodegenCtx *ctx, ReturnNode *node);

// --- Flow Handlers ---
void codegen_func_def(CodegenCtx *ctx, FuncDefNode *node);
void codegen_loop(CodegenCtx *ctx, LoopNode *node);
void codegen_while(CodegenCtx *ctx, WhileNode *node);
void codegen_if(CodegenCtx *ctx, IfNode *node);
void codegen_break(CodegenCtx *ctx);
void codegen_continue(CodegenCtx *ctx);

#endif
