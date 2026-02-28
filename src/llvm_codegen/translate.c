#include "translate.h"

void translate_inst(CodegenCtx *ctx, AlirInst *inst) {
    LLVMValueRef op1 = get_llvm_value(ctx, inst->op1);
    LLVMValueRef op2 = get_llvm_value(ctx, inst->op2);
    LLVMValueRef res = NULL;

    int is_float = (inst->op1 && (inst->op1->type.base == TYPE_FLOAT || inst->op1->type.base == TYPE_DOUBLE));

    switch (inst->op) {
        case ALIR_OP_ALLOCA: {
            LLVMTypeRef ty = get_llvm_type(ctx, inst->dest->type);
            res = LLVMBuildAlloca(ctx->builder, ty, "alloc");
            break;
        }
        case ALIR_OP_STORE: {
            if (op1 && op2) {
                LLVMValueRef val = op1;
                LLVMValueRef ptr = op2;
                
                if (LLVMGetTypeKind(LLVMTypeOf(ptr)) != LLVMPointerTypeKind) {
                    ptr = LLVMBuildIntToPtr(ctx->builder, ptr, LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0), "store_cast");
                }
                LLVMBuildStore(ctx->builder, val, ptr);
            }
            break;
        }
        case ALIR_OP_LOAD: {
            if (op1) {
                LLVMTypeRef ty = get_llvm_type(ctx, inst->dest->type);
                if (LLVMGetTypeKind(LLVMTypeOf(op1)) != LLVMPointerTypeKind) {
                    op1 = LLVMBuildIntToPtr(ctx->builder, op1, LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0), "load_cast");
                }
                res = LLVMBuildLoad2(ctx->builder, ty, op1, "load");
            }
            break;
        }
        case ALIR_OP_GET_PTR: {
            if (!op1) break;
            
            // Validate GEP input strictly
            if (LLVMGetTypeKind(LLVMTypeOf(op1)) != LLVMPointerTypeKind) {
                op1 = LLVMBuildIntToPtr(ctx->builder, op1, LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0), "safe_cast");
            }

            VarType ptr_t = inst->op1->type;
            if (ptr_t.ptr_depth > 0) ptr_t.ptr_depth--;
            else if (ptr_t.array_size > 0) ptr_t.array_size = 0; // Natural Array decay
            
            LLVMTypeRef base_ty = get_llvm_type(ctx, ptr_t);
            
            // Differentiate Struct GEP (Constant Index) vs Array GEP
            if (ptr_t.base == TYPE_CLASS && ptr_t.ptr_depth == 0 && inst->op2 && inst->op2->kind == ALIR_VAL_CONST) {
                res = LLVMBuildStructGEP2(ctx->builder, base_ty, op1, (unsigned)inst->op2->int_val, "struct_gep");
            } else {
                if (inst->op1->type.array_size > 0) {
                    // Proper LLVM GEP indexing for explicit Array types ([N x i32]*)
                    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx->llvm_ctx), 0, 0);
                    LLVMValueRef indices[] = { zero, op2 };
                    LLVMTypeRef arr_ty = get_llvm_type(ctx, inst->op1->type);
                    res = LLVMBuildGEP2(ctx->builder, arr_ty, op1, indices, 2, "array_gep");
                } else {
                    // Standard Pointer iteration (i32*)
                    LLVMValueRef indices[] = { op2 };
                    res = LLVMBuildGEP2(ctx->builder, base_ty, op1, indices, 1, "ptr_gep");
                }
            }
            break;
        }
        
        // Math Ops
        case ALIR_OP_ADD: res = LLVMBuildAdd(ctx->builder, op1, op2, "add"); break;
        case ALIR_OP_SUB: res = LLVMBuildSub(ctx->builder, op1, op2, "sub"); break;
        case ALIR_OP_MUL: res = LLVMBuildMul(ctx->builder, op1, op2, "mul"); break;
        case ALIR_OP_DIV: res = LLVMBuildSDiv(ctx->builder, op1, op2, "div"); break;
        case ALIR_OP_MOD: res = LLVMBuildSRem(ctx->builder, op1, op2, "mod"); break;
        
        case ALIR_OP_FADD: res = LLVMBuildFAdd(ctx->builder, op1, op2, "fadd"); break;
        case ALIR_OP_FSUB: res = LLVMBuildFSub(ctx->builder, op1, op2, "fsub"); break;
        case ALIR_OP_FMUL: res = LLVMBuildFMul(ctx->builder, op1, op2, "fmul"); break;
        case ALIR_OP_FDIV: res = LLVMBuildFDiv(ctx->builder, op1, op2, "fdiv"); break;
        
        // Logical
        case ALIR_OP_AND: res = LLVMBuildAnd(ctx->builder, op1, op2, "and"); break;
        case ALIR_OP_OR:  res = LLVMBuildOr(ctx->builder, op1, op2, "or"); break;
        case ALIR_OP_XOR: res = LLVMBuildXor(ctx->builder, op1, op2, "xor"); break;
        case ALIR_OP_SHL: res = LLVMBuildShl(ctx->builder, op1, op2, "shl"); break;
        case ALIR_OP_SHR: res = LLVMBuildAShr(ctx->builder, op1, op2, "shr"); break;
        case ALIR_OP_NOT: res = (LLVMGetTypeKind(LLVMTypeOf(op1)) == LLVMPointerTypeKind) ? LLVMBuildIsNull(ctx->builder, op1, "isnull") : LLVMBuildNot(ctx->builder, op1, "not"); break;
        
        // Comparisons
        case ALIR_OP_EQ:  res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, op1, op2, "feq") : LLVMBuildICmp(ctx->builder, LLVMIntEQ, op1, op2, "ieq"); break;
        case ALIR_OP_NEQ: res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, op1, op2, "fne") : LLVMBuildICmp(ctx->builder, LLVMIntNE, op1, op2, "ine"); break;
        case ALIR_OP_LT:  res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, op1, op2, "flt") : LLVMBuildICmp(ctx->builder, LLVMIntSLT, op1, op2, "ilt"); break;
        case ALIR_OP_GT:  res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, op1, op2, "fgt") : LLVMBuildICmp(ctx->builder, LLVMIntSGT, op1, op2, "igt"); break;
        case ALIR_OP_LTE: res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, op1, op2, "fle") : LLVMBuildICmp(ctx->builder, LLVMIntSLE, op1, op2, "ile"); break;
        case ALIR_OP_GTE: res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, op1, op2, "fge") : LLVMBuildICmp(ctx->builder, LLVMIntSGE, op1, op2, "ige"); break;

        // Flow Control
        case ALIR_OP_JUMP: {
            LLVMBasicBlockRef dest_bb = hashmap_get(&ctx->block_map, inst->op1->str_val);
            if (dest_bb) LLVMBuildBr(ctx->builder, dest_bb);
            break;
        }
        case ALIR_OP_CONDI: {
            LLVMBasicBlockRef then_bb = hashmap_get(&ctx->block_map, inst->op2->str_val);
            LLVMBasicBlockRef else_bb = hashmap_get(&ctx->block_map, inst->args[0]->str_val);
            if (then_bb && else_bb && op1) LLVMBuildCondBr(ctx->builder, op1, then_bb, else_bb);
            break;
        }
        case ALIR_OP_SWITCH: {
            LLVMBasicBlockRef default_bb = hashmap_get(&ctx->block_map, inst->op2->str_val);
            int num_cases = 0;
            for(AlirSwitchCase *c = inst->cases; c; c = c->next) num_cases++;
            
            if (op1 && default_bb) {
                res = LLVMBuildSwitch(ctx->builder, op1, default_bb, num_cases);
                for(AlirSwitchCase *c = inst->cases; c; c = c->next) {
                    LLVMValueRef case_val = LLVMConstInt(get_llvm_type(ctx, inst->op1->type), c->value, 0);
                    LLVMBasicBlockRef case_bb = hashmap_get(&ctx->block_map, c->label);
                    if (case_bb) LLVMAddCase(res, case_val, case_bb);
                }
            }
            break;
        }
        case ALIR_OP_CALL: {
            LLVMValueRef func = NULL;
            LLVMTypeRef func_ty = NULL;
            
            if (inst->op1 && inst->op1->str_val) {
                func = LLVMGetNamedFunction(ctx->llvm_mod, inst->op1->str_val);
                if (func) func_ty = LLVMGlobalGetValueType(func);
            }

            if (!func && inst->op1 && inst->op1->str_val) {
                func = hashmap_get(&ctx->func_map, inst->op1->str_val);
                func_ty = hashmap_get(&ctx->func_type_map, inst->op1->str_val);
                
                if (!func) {
                    LLVMTypeRef ret_ty = inst->dest ? get_llvm_type(ctx, inst->dest->type) : LLVMVoidTypeInContext(ctx->llvm_ctx);
                    func_ty = LLVMFunctionType(ret_ty, NULL, 0, 1); // Vararg fallback
                    func = LLVMAddFunction(ctx->llvm_mod, inst->op1->str_val, func_ty);
                }
            }

            if (!func) break;
            
            LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * inst->arg_count);
            for(int i = 0; i < inst->arg_count; i++) {
                args[i] = get_llvm_value(ctx, inst->args[i]);
                if (!args[i]) {
                    // Safety for unresolved arguments
                    args[i] = LLVMConstInt(LLVMInt64TypeInContext(ctx->llvm_ctx), 0, 0);
                }
                
                // CRITICAL: Prevent truncation for varargs on 64-bit systems.
                // If the argument is an integer, promote to 64-bit word size if it's potentially a pointer.
                LLVMTypeRef arg_ty = LLVMTypeOf(args[i]);
                if (LLVMGetTypeKind(arg_ty) == LLVMIntegerTypeKind) {
                    if (inst->args[i]->type.base == TYPE_UNKNOWN || inst->args[i]->type.base == TYPE_AUTO) {
                         if (LLVMGetIntTypeWidth(arg_ty) < 64) {
                             args[i] = LLVMBuildZExt(ctx->builder, args[i], LLVMInt64TypeInContext(ctx->llvm_ctx), "prom_word");
                         }
                    } else if (LLVMGetIntTypeWidth(arg_ty) < 32) {
                        args[i] = LLVMBuildSExt(ctx->builder, args[i], LLVMInt32TypeInContext(ctx->llvm_ctx), "prom_i32");
                    }
                }
            }

            res = LLVMBuildCall2(ctx->builder, func_ty, func, args, inst->arg_count, (LLVMGetReturnType(func_ty) == LLVMVoidTypeInContext(ctx->llvm_ctx)) ? "" : "call");
            free(args);
            break;
        }
        case ALIR_OP_RET: {
            if (op1) {
                LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
                LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
                LLVMTypeRef current_func_ty = LLVMGlobalGetValueType(current_func);
                if (LLVMGetReturnType(current_func_ty) == LLVMVoidTypeInContext(ctx->llvm_ctx)) {
                    LLVMBuildRetVoid(ctx->builder);
                } else {
                    LLVMBuildRet(ctx->builder, op1);
                }
            } else {
                LLVMBuildRetVoid(ctx->builder);
            }
            break;
        }
        
        // Conversions and Casts
        case ALIR_OP_BITCAST: {
            if (op1) {
                LLVMTypeRef dest_ty = get_llvm_type(ctx, inst->dest->type);
                LLVMTypeKind op1_k = LLVMGetTypeKind(LLVMTypeOf(op1));
                LLVMTypeKind dest_k = LLVMGetTypeKind(dest_ty);
                
                // Safe bitcasting routines
                if (op1_k == LLVMIntegerTypeKind && dest_k == LLVMPointerTypeKind) {
                    res = LLVMBuildIntToPtr(ctx->builder, op1, dest_ty, "inttoptr");
                } else if (op1_k == LLVMPointerTypeKind && dest_k == LLVMIntegerTypeKind) {
                    res = LLVMBuildPtrToInt(ctx->builder, op1, dest_ty, "ptrtoint");
                } else if (op1_k == dest_k) {
                    res = op1; // Opaque pointers handle this directly
                } else {
                    res = LLVMBuildBitCast(ctx->builder, op1, dest_ty, "bitcast");
                }
            }
            break;
        }
        case ALIR_OP_CAST: {
            if (!op1) break;
            LLVMTypeRef dest_ty = get_llvm_type(ctx, inst->dest->type);
            if (is_float) {
                if (inst->dest->type.base == TYPE_FLOAT || inst->dest->type.base == TYPE_DOUBLE) {
                    res = LLVMBuildFPCast(ctx->builder, op1, dest_ty, "fpcast");
                } else {
                    res = LLVMBuildFPToSI(ctx->builder, op1, dest_ty, "fptosi");
                }
            } else {
                if (inst->dest->type.base == TYPE_FLOAT || inst->dest->type.base == TYPE_DOUBLE) {
                    res = LLVMBuildSIToFP(ctx->builder, op1, dest_ty, "sitofp");
                } else {
                    res = LLVMBuildIntCast(ctx->builder, op1, dest_ty, "intcast");
                }
            }
            break;
        }
        
        // Low Level Memory Overrides
        case ALIR_OP_ALLOC_HEAP: {
            if (op1) {
                LLVMValueRef size = op1;
                // malloc expects size_t (i64 on 64-bit systems)
                if (LLVMGetTypeKind(LLVMTypeOf(size)) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(LLVMTypeOf(size)) < 64) {
                    size = LLVMBuildZExt(ctx->builder, size, LLVMInt64TypeInContext(ctx->llvm_ctx), "sz_ext");
                }
                res = LLVMBuildArrayMalloc(ctx->builder, LLVMInt8TypeInContext(ctx->llvm_ctx), size, "malloc");
            }
            break;
        }
        case ALIR_OP_SIZEOF: {
            LLVMTypeRef ty = get_llvm_type(ctx, inst->op1->type);
            res = LLVMSizeOf(ty);
            LLVMTypeRef dest_ty = inst->dest ? get_llvm_type(ctx, inst->dest->type) : LLVMInt64TypeInContext(ctx->llvm_ctx);
            res = LLVMBuildIntCast(ctx->builder, res, dest_ty, "sz_cast");
            break;
        }
        case ALIR_OP_FREE_HEAP: {
            if (op1) LLVMBuildFree(ctx->builder, op1);
            break;
        }
        case ALIR_OP_MOV: {
            res = op1; // Assign value directly (Register Aliasing)
            break;
        }

        // Native Abstract Iterators Lowering 
        case ALIR_OP_ITER_INIT: {
            llvm_codegen_flux_iter_init(ctx, inst, op1, &res);
            break;
        }
        case ALIR_OP_ITER_VALID: {
            llvm_codegen_flux_iter_valid(ctx, op1, &res);
            break;
        }
        case ALIR_OP_ITER_GET: {
            llvm_codegen_flux_iter_get(ctx, inst, op1, &res);
            break;
        }
        case ALIR_OP_ITER_NEXT: {
            llvm_codegen_flux_iter_next(ctx, op1); 
            break;
        }

        // Catch explicitly untranslated features or flux opcodes mapped out by ALIR
        // TODO alir free stack
        // ALIR_OP_YIELD todo <--- just use gemini for this shit fuck you I am too lazy coding
        // ALIR_OP_PHI todo
        default: break; 
    }

    if (inst->dest && res) {
        set_llvm_value(ctx, inst->dest, res);
    }

}
