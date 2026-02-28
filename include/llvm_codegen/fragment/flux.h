#ifndef LLVM_CODEGEN_FRAGMENT_FLUX_H
#define LLVM_CODEGEN_FRAGMENT_FLUX_H

#include "../codegen.h"

void llvm_codegen_flux_iter_get(CodegenCtx *ctx, AlirInst *inst, LLVMValueRef op1, LLVMValueRef *res);
void llvm_codegen_flux_iter_next(CodegenCtx *ctx, LLVMValueRef op1);
void llvm_codegen_flux_iter_valid(CodegenCtx *ctx, LLVMValueRef op1, LLVMValueRef *res);
void llvm_codegen_flux_iter_init(CodegenCtx *ctx, AlirInst *inst, LLVMValueRef op1, LLVMValueRef *res);

#endif // LLVM_CODEGEN_FRAGMENT_FLUX_H
