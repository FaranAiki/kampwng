#ifndef CODEGEN_H
#define CODEGEN_H

#include "../parser/parser.h"
#include "../semantic/semantic.h"
#include <stdbool.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>

typedef struct Symbol {
  char *name;
  LLVMValueRef value;
  LLVMTypeRef type; 
  VarType vtype;    
  int is_array;
  int is_mutable;
  int is_direct_value; // True for Enum constants (r-values)
  struct Symbol *next;
} Symbol;

typedef struct FuncSymbol {
    char *name;
    VarType ret_type;
    VarType *param_types; // Added: for overload resolution checks in codegen
    int param_count;      // Added
    int is_flux;          // Added: Track if this is a generator
    VarType yield_type;   // Added: Stores the original yield type for Flux functions
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

// Track where traits begin in the struct layout
typedef struct TraitOffset {
    char *trait_name;
    int offset_index; 
    struct TraitOffset *next;
} TraitOffset;

typedef struct ClassInfo {
    char *name;
    char *parent_name; 
    LLVMTypeRef struct_type;
    ClassMember *members;
    
    // Trait Support
    char **trait_names;
    int trait_count;
    TraitOffset *trait_offsets;

    // Metadata for reflection (hasmethod)
    char **method_names;
    int method_count;
    
    int is_extern; // Supports opaque types
    int is_union;  // Support unions

    struct ClassInfo *next;
} ClassInfo;

typedef struct EnumEntryInfo {
    char *name;
    int value;
    struct EnumEntryInfo *next;
} EnumEntryInfo;

typedef struct EnumInfo {
    char *name;
    EnumEntryInfo *entries;
    LLVMValueRef to_string_func;
    struct EnumInfo *next;
} EnumInfo;

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
  EnumInfo *enums; 
  LoopContext *current_loop; 
  
  // Namespacing
  char *current_prefix;
  char **known_namespaces;
  int known_namespace_count;
  
  // For error reporting
  const char *source_code;

  // these are builtins 
  // as string compare need these
  LLVMTypeRef printf_type;
  LLVMValueRef printf_func;
  LLVMValueRef input_func;
  LLVMValueRef strcmp_func;
  LLVMValueRef malloc_func;
  LLVMValueRef calloc_func;
  LLVMValueRef free_func;
  LLVMValueRef strlen_func;
  LLVMValueRef strcpy_func;
  LLVMValueRef strdup_func;
  
  // setjmp/longjmp support
  LLVMValueRef setjmp_func;
  LLVMValueRef longjmp_func;

  // Flux / Generator Support (LLVM Coroutines)
  LLVMValueRef flux_promise_val; // Pointer to the Promise alloca in current flux func
  LLVMTypeRef flux_promise_type; // Explicit tracking of promise struct type (avoids opaque ptr issues)
  LLVMValueRef flux_coro_hdl;    // Added: Current Coroutine Handle (i8*)
  LLVMBasicBlockRef flux_return_block; // Cleanup block to jump to on return

  // LLVM Coroutine Intrinsics
  LLVMValueRef coro_id;
  LLVMValueRef coro_size;
  LLVMValueRef coro_begin;
  LLVMValueRef coro_save;    // Added: llvm.coro.save
  LLVMValueRef coro_suspend;
  LLVMValueRef coro_end;
  LLVMValueRef coro_free;
  LLVMValueRef coro_resume;
  LLVMValueRef coro_destroy;
  LLVMValueRef coro_promise;
  LLVMValueRef coro_done;

} CodegenCtx;

// --- Core API ---
void codegen_init_ctx(CodegenCtx *ctx, LLVMModuleRef module, LLVMBuilderRef builder, const char *source);
LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name, const char *source);

// --- Error Helper ---
void codegen_error(CodegenCtx *ctx, ASTNode *node, const char *msg);

// --- Shared Internal ---
void add_symbol(CodegenCtx *ctx, const char *name, LLVMValueRef val, LLVMTypeRef type, VarType vtype, int is_array, int is_mut);
Symbol* find_symbol(CodegenCtx *ctx, const char *name);
void add_func_symbol(CodegenCtx *ctx, const char *name, VarType ret_type, VarType *params, int pcount, int is_flux);
FuncSymbol* find_func_symbol(CodegenCtx *ctx, const char *name);

// Namespace Helpers
void add_namespace_name(CodegenCtx *ctx, const char *name);
int is_namespace(CodegenCtx *ctx, const char *name);

// Class Helpers
void add_class_info(CodegenCtx *ctx, ClassInfo *ci);
ClassInfo* find_class(CodegenCtx *ctx, const char *name);
int get_member_index(ClassInfo *ci, const char *member, LLVMTypeRef *out_type, VarType *out_vtype);

// FIX: Added ctx to signature to prevent NULL context crash during parent lookup
int get_trait_offset(CodegenCtx *ctx, ClassInfo *ci, const char *trait_name);

// Enum Helpers
void add_enum_info(CodegenCtx *ctx, EnumInfo *ei);
EnumInfo* find_enum(CodegenCtx *ctx, const char *name);

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
void codegen_switch(CodegenCtx *ctx, SwitchNode *node);
void codegen_break(CodegenCtx *ctx);
void codegen_continue(CodegenCtx *ctx);

// Flux Handlers
void codegen_flux_def(CodegenCtx *ctx, FuncDefNode *node);
void codegen_emit(CodegenCtx *ctx, EmitNode *node);
void codegen_for_in(CodegenCtx *ctx, ForInNode *node);

// builtin
LLVMValueRef generate_strlen(LLVMModuleRef module);
LLVMValueRef generate_strcpy(LLVMModuleRef module);
LLVMValueRef generate_strdup(LLVMModuleRef module, LLVMValueRef malloc_func, LLVMValueRef strlen_func, LLVMValueRef strcpy_func);
LLVMValueRef generate_input_func(LLVMModuleRef module, LLVMBuilderRef builder, LLVMValueRef malloc_func, LLVMValueRef getchar_func);
LLVMValueRef generate_enum_to_string_func(CodegenCtx *ctx, EnumInfo *ei);

char* format_string(const char* input);
void get_type_name(VarType t, char *buf);

LLVMTypeRef get_lvalue_struct_type(CodegenCtx *ctx, ASTNode *node); 

LLVMValueRef gen_call(CodegenCtx *ctx, CallNode *node);
LLVMValueRef gen_method_call(CodegenCtx *ctx, MethodCallNode *node);
LLVMValueRef gen_trait_access(CodegenCtx *ctx, TraitAccessNode *node);
LLVMValueRef gen_array_lit(CodegenCtx *ctx, ArrayLitNode *node);
LLVMValueRef gen_literal(CodegenCtx *ctx, LiteralNode *node);
LLVMValueRef gen_binary_op(CodegenCtx *ctx, BinaryOpNode *node);
LLVMValueRef gen_unary_op(CodegenCtx *ctx, UnaryOpNode *node);
LLVMValueRef gen_array_access(CodegenCtx *ctx, ArrayAccessNode *node);
LLVMValueRef gen_member_access(CodegenCtx *ctx, MemberAccessNode *node);
LLVMValueRef gen_inc_dec(CodegenCtx *ctx, IncDecNode *node);
LLVMValueRef gen_var_ref(CodegenCtx *ctx, VarRefNode *node);
LLVMValueRef gen_typeof(CodegenCtx *ctx, UnaryOpNode *node);
LLVMValueRef gen_reflection(CodegenCtx *ctx, UnaryOpNode *node, int is_method);
LLVMValueRef gen_cast(CodegenCtx *ctx, CastNode *node);

// Helpers for binary ops (exposed for compound assignment)
LLVMValueRef llvm_build_bin_op(CodegenCtx *ctx, LLVMValueRef l, LLVMValueRef r, int op, VarType lt, VarType rt);

LLVMValueRef generate_enum_to_string_func(CodegenCtx *ctx, EnumInfo *ei);

#endif
