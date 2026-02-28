#ifndef LLVM_CODEGEN_H
#define LLVM_CODEGEN_H

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include "../../include/alir/alir.h"

typedef struct CodegenCtx {
    AlirModule *alir_mod;
    LLVMContextRef llvm_ctx;
    LLVMModuleRef llvm_mod;
    LLVMBuilderRef builder;

    HashMap value_map;      // Maps: Name -> LLVMValueRef (For locals/params)
    LLVMValueRef *temps;    // Maps: temp_id -> LLVMValueRef
    int max_temps;

    HashMap block_map;      // Maps: Label -> LLVMBasicBlockRef
    HashMap struct_map;     // Maps: Class/Struct Name -> LLVMTypeRef
    HashMap func_map;       // Maps: Function Name -> LLVMValueRef
    HashMap func_type_map;  // Maps: Function Name -> LLVMTypeRef

    Arena *arena;           // Borrowed from compiler context
} CodegenCtx;

// Initialize the code generator for a given ALIR Module
CodegenCtx* codegen_init(AlirModule *mod);

// Run the multi-pass translation (Structs -> Globals -> Prototypes -> Bodies)
// Returns the built LLVMModuleRef ready for JIT/AOT compilation.
LLVMModuleRef codegen_generate(CodegenCtx *ctx);

// Emit the LLVM IR to a file
void codegen_emit_to_file(CodegenCtx *ctx, const char *filename);

// Print the LLVM IR to stdout
void codegen_print(CodegenCtx *ctx);

// Clean up the code generator resources (Note: does not free the LLVMModuleRef)
void codegen_dispose(CodegenCtx *ctx);

void set_llvm_value(CodegenCtx *ctx, AlirValue *v, LLVMValueRef llvm_val);

LLVMTypeRef get_llvm_type(CodegenCtx *ctx, VarType t);

LLVMValueRef get_llvm_value(CodegenCtx *ctx, AlirValue *v);

#include "translate.h"
#include "fragment/flux.h"

#endif // LLVM_CODEGEN_H
