#ifndef LLVM_CODEGEN_H
#define LLVM_CODEGEN_H

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include "../../include/alir/alir.h"

// The main context for translating ALIR into LLVM IR
typedef struct CodegenCtx CodegenCtx;

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

#endif // LLVM_CODEGEN_H
