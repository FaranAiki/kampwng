#include "codegen.h"

void llvm_codegen_flux_iter_init(CodegenCtx *ctx, AlirInst *inst, LLVMValueRef op1, LLVMValueRef *res) {
    LLVMTypeRef i64_ty = LLVMInt64TypeInContext(ctx->llvm_ctx);
    LLVMTypeRef ptr_ty = LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0);
    
    // Standardizing an inline iterator state block: { i8* base, i64 max_length, i64 current_index }
    LLVMTypeRef iter_struct_ty = LLVMStructTypeInContext(ctx->llvm_ctx, (LLVMTypeRef[]){ptr_ty, i64_ty, i64_ty}, 3, 0);

    *res = LLVMBuildAlloca(ctx->builder, iter_struct_ty, "iter_state");

    // 1. Force the Collection to 
    LLVMValueRef base_ptr = op1;
    if (base_ptr) {
        if (LLVMGetTypeKind(LLVMTypeOf(base_ptr)) != LLVMPointerTypeKind) {
            base_ptr = LLVMBuildIntToPtr(ctx->builder, base_ptr, ptr_ty, "iter_base_cast");
        } else {
            base_ptr = LLVMBuildBitCast(ctx->builder, base_ptr, ptr_ty, "iter_base_cast");
        }
    } else {
        base_ptr = LLVMConstPointerNull(ptr_ty);
    }

    // 2. Discover max limits
    LLVMValueRef length;
    if (inst->op1 && inst->op1->type.array_size > 0) {
        // Resolved Static Array Sizes
        length = LLVMConstInt(i64_ty, inst->op1->type.array_size, 0);
    } else {
        // If it decayed to a generic pointer dynamically, allow unbounded looping till INT_MAX.
        // Works perfectly for manual `break` implementations and maps gracefully to standard C behaviors
        length = LLVMConstInt(i64_ty, 0x7FFFFFFF, 0); 
    }

    LLVMValueRef p_base = LLVMBuildStructGEP2(ctx->builder, iter_struct_ty, *res, 0, "p_base");
    LLVMBuildStore(ctx->builder, base_ptr, p_base);

    LLVMValueRef p_len = LLVMBuildStructGEP2(ctx->builder, iter_struct_ty, *res, 1, "p_len");
    LLVMBuildStore(ctx->builder, length, p_len);

    LLVMValueRef p_idx = LLVMBuildStructGEP2(ctx->builder, iter_struct_ty, *res, 2, "p_idx");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i64_ty, 0, 0), p_idx);
}

void llvm_codegen_flux_iter_get(CodegenCtx *ctx, AlirInst *inst, LLVMValueRef op1, LLVMValueRef *res) {
    LLVMTypeRef i64_ty = LLVMInt64TypeInContext(ctx->llvm_ctx);
    LLVMTypeRef ptr_ty = LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0);
    LLVMTypeRef iter_struct_ty = LLVMStructTypeInContext(ctx->llvm_ctx, (LLVMTypeRef[]){ptr_ty, i64_ty, i64_ty}, 3, 0);

    if (op1 && inst->dest) {
        LLVMValueRef p_base = LLVMBuildStructGEP2(ctx->builder, iter_struct_ty, op1, 0, "p_base");
        LLVMValueRef base = LLVMBuildLoad2(ctx->builder, ptr_ty, p_base, "base");

        LLVMValueRef p_idx = LLVMBuildStructGEP2(ctx->builder, iter_struct_ty, op1, 2, "p_idx");
        LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, i64_ty, p_idx, "idx");

        // GEP via type casting up to the expected returned collection element class/prim
        LLVMTypeRef elem_ty = get_llvm_type(ctx, inst->dest->type);
        LLVMValueRef typed_base = LLVMBuildBitCast(ctx->builder, base, LLVMPointerType(elem_ty, 0), "typed_base");
        
        LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, elem_ty, typed_base, &idx, 1, "elem_ptr");
        *res = LLVMBuildLoad2(ctx->builder, elem_ty, elem_ptr, "iter_val");
    }
}

void llvm_codegen_flux_iter_next(CodegenCtx *ctx, LLVMValueRef op1) {
    LLVMTypeRef i64_ty = LLVMInt64TypeInContext(ctx->llvm_ctx);
    LLVMTypeRef ptr_ty = LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0);
    LLVMTypeRef iter_struct_ty = LLVMStructTypeInContext(ctx->llvm_ctx, (LLVMTypeRef[]){ptr_ty, i64_ty, i64_ty}, 3, 0);

    if (op1) {
        LLVMValueRef p_idx = LLVMBuildStructGEP2(ctx->builder, iter_struct_ty, op1, 2, "p_idx");
        LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, i64_ty, p_idx, "idx");

        LLVMValueRef next_idx = LLVMBuildAdd(ctx->builder, idx, LLVMConstInt(i64_ty, 1, 0), "next_idx");
        LLVMBuildStore(ctx->builder, next_idx, p_idx);
    }
}

void llvm_codegen_flux_iter_valid(CodegenCtx *ctx, LLVMValueRef op1, LLVMValueRef *res) {
    LLVMTypeRef i64_ty = LLVMInt64TypeInContext(ctx->llvm_ctx);
    LLVMTypeRef ptr_ty = LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0);
    LLVMTypeRef iter_struct_ty = LLVMStructTypeInContext(ctx->llvm_ctx, (LLVMTypeRef[]){ptr_ty, i64_ty, i64_ty}, 3, 0);

    if (op1) {
        LLVMValueRef p_idx = LLVMBuildStructGEP2(ctx->builder, iter_struct_ty, op1, 2, "p_idx");
        LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, i64_ty, p_idx, "idx");

        LLVMValueRef p_len = LLVMBuildStructGEP2(ctx->builder, iter_struct_ty, op1, 1, "p_len");
        LLVMValueRef len = LLVMBuildLoad2(ctx->builder, i64_ty, p_len, "len");

        *res = LLVMBuildICmp(ctx->builder, LLVMIntSLT, idx, len, "iter_valid");
    } else {
        *res = LLVMConstInt(LLVMInt1TypeInContext(ctx->llvm_ctx), 0, 0);
    }
}
