#include "../../include/llvm_codegen/codegen.h"
#include "../../include/common/hashmap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct CodegenCtx {
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
};

CodegenCtx* codegen_init(AlirModule *mod) {
    CodegenCtx *ctx = calloc(1, sizeof(CodegenCtx));
    ctx->alir_mod = mod;
    ctx->llvm_ctx = LLVMContextCreate();
    ctx->llvm_mod = LLVMModuleCreateWithNameInContext(mod->name ? mod->name : "alick_module", ctx->llvm_ctx);
    ctx->builder = LLVMCreateBuilderInContext(ctx->llvm_ctx);
    
    ctx->arena = mod->compiler_ctx ? mod->compiler_ctx->arena : NULL;

    // Initialize resolution maps
    hashmap_init(&ctx->value_map, ctx->arena, 256);
    hashmap_init(&ctx->block_map, ctx->arena, 256);
    hashmap_init(&ctx->struct_map, ctx->arena, 64);
    hashmap_init(&ctx->func_map, ctx->arena, 64);
    hashmap_init(&ctx->func_type_map, ctx->arena, 64);

    return ctx;
}

void codegen_dispose(CodegenCtx *ctx) {
    if (!ctx) return;
    LLVMDisposeBuilder(ctx->builder);
    
    // Note: To preserve LLVMModule for execution/JIT, we only clean up the builder.
    // The module and LLVMContext will need to be disposed later by the driver.
    free(ctx);
}

static LLVMTypeRef get_llvm_type(CodegenCtx *ctx, VarType t) {
    LLVMTypeRef base = NULL;
    switch (t.base) {
        case TYPE_VOID: base = LLVMVoidTypeInContext(ctx->llvm_ctx); break;
        case TYPE_INT: base = LLVMInt32TypeInContext(ctx->llvm_ctx); break;
        case TYPE_SHORT: base = LLVMInt16TypeInContext(ctx->llvm_ctx); break;
        case TYPE_LONG: base = LLVMInt64TypeInContext(ctx->llvm_ctx); break;
        case TYPE_LONG_LONG: base = LLVMInt64TypeInContext(ctx->llvm_ctx); break;
        case TYPE_CHAR: base = LLVMInt8TypeInContext(ctx->llvm_ctx); break;
        case TYPE_BOOL: base = LLVMInt1TypeInContext(ctx->llvm_ctx); break;
        case TYPE_FLOAT: base = LLVMFloatTypeInContext(ctx->llvm_ctx); break;
        case TYPE_DOUBLE: 
        case TYPE_LONG_DOUBLE: base = LLVMDoubleTypeInContext(ctx->llvm_ctx); break;
        case TYPE_STRING: base = LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0); break;
        case TYPE_CLASS: {
            if (t.class_name) {
                base = hashmap_get(&ctx->struct_map, t.class_name);
                if (!base) {
                    base = LLVMStructCreateNamed(ctx->llvm_ctx, t.class_name);
                    hashmap_put(&ctx->struct_map, t.class_name, base);
                }
            } else {
                base = LLVMInt8TypeInContext(ctx->llvm_ctx); // Opaque fallback
            }
            break;
        }
        case TYPE_ENUM: base = LLVMInt32TypeInContext(ctx->llvm_ctx); break;
        default: base = LLVMInt32TypeInContext(ctx->llvm_ctx); break; // Fallback to int
    }

    // Wrap with requested pointer depth
    for (int i = 0; i < t.ptr_depth; i++) {
        if (base == LLVMVoidTypeInContext(ctx->llvm_ctx)) {
            base = LLVMInt8TypeInContext(ctx->llvm_ctx); // void* becomes i8* natively
        }
        base = LLVMPointerType(base, 0);
    }
    
    if (t.array_size > 0) {
        base = LLVMArrayType(base, t.array_size);
    }
    
    return base;
}

static void set_llvm_value(CodegenCtx *ctx, AlirValue *v, LLVMValueRef llvm_val) {
    if (!v) return;
    if (v->kind == ALIR_VAL_TEMP) {
        if (v->temp_id < ctx->max_temps) {
            ctx->temps[v->temp_id] = llvm_val;
        }
    } else if (v->kind == ALIR_VAL_VAR) {
        hashmap_put(&ctx->value_map, v->str_val, llvm_val);
    }
}

static LLVMValueRef get_llvm_value(CodegenCtx *ctx, AlirValue *v) {
    if (!v) return NULL;
    
    switch (v->kind) {
        case ALIR_VAL_CONST: {
            LLVMTypeRef ty = get_llvm_type(ctx, v->type);
            if (v->type.base == TYPE_FLOAT || v->type.base == TYPE_DOUBLE) {
                return LLVMConstReal(ty, v->float_val);
            } else {
                return LLVMConstInt(ty, v->int_val, !v->type.is_unsigned);
            }
        }
        case ALIR_VAL_TEMP:
            if (v->temp_id < ctx->max_temps) return ctx->temps[v->temp_id];
            return NULL;
            
        case ALIR_VAL_VAR:
            return hashmap_get(&ctx->value_map, v->str_val);
            
        case ALIR_VAL_GLOBAL: {
            // First check if it's a global variable
            LLVMValueRef glob = LLVMGetNamedGlobal(ctx->llvm_mod, v->str_val);
            if (!glob) {
                // If not found, it might be a function masquerading as a global value
                glob = LLVMGetNamedFunction(ctx->llvm_mod, v->str_val);
            }

            // Strings decay gracefully to pointers via BitCast
            if (glob && v->type.base == TYPE_STRING) {
                LLVMTypeRef ptr_ty = LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0);
                return LLVMConstBitCast(glob, ptr_ty);
            }
            return glob;
        }
        case ALIR_VAL_TYPE:
            // SIZEOF needs the actual LLVMType, not a value. We handle this inside translation.
            return NULL;
        case ALIR_VAL_LABEL:
            return NULL;
        default: return NULL;
    }
}

static void translate_inst(CodegenCtx *ctx, AlirInst *inst) {
    LLVMValueRef op1 = get_llvm_value(ctx, inst->op1);
    LLVMValueRef op2 = get_llvm_value(ctx, inst->op2);
    LLVMValueRef res = NULL;

    int is_float = (inst->op1 && (inst->op1->type.base == TYPE_FLOAT || inst->op1->type.base == TYPE_DOUBLE));

    switch (inst->op) {
        case ALIR_OP_ALLOCA: {
            VarType elem_t = inst->dest->type;
            elem_t.ptr_depth--; // ALIR dest is already a pointer
            res = LLVMBuildAlloca(ctx->builder, get_llvm_type(ctx, elem_t), "alloc");
            break;
        }
        case ALIR_OP_STORE: {
            if (op1 && op2) {
                LLVMBuildStore(ctx->builder, op1, op2);
            }
            break;
        }
        case ALIR_OP_LOAD: {
            VarType elem_t = inst->dest->type;
            if (op1) {
                res = LLVMBuildLoad2(ctx->builder, get_llvm_type(ctx, elem_t), op1, "load");
            }
            break;
        }
        case ALIR_OP_GET_PTR: {
            if (!op1) break;
            VarType ptr_t = inst->op1->type;
            ptr_t.ptr_depth--;
            LLVMTypeRef base_ty = get_llvm_type(ctx, ptr_t);
            
            // Differentiate Struct GEP (Constant Index) vs Array GEP
            if (ptr_t.base == TYPE_CLASS && ptr_t.ptr_depth == 0 && inst->op2 && inst->op2->kind == ALIR_VAL_CONST) {
                res = LLVMBuildStructGEP2(ctx->builder, base_ty, op1, (unsigned)inst->op2->int_val, "struct_gep");
            } else {
                LLVMValueRef indices[] = { op2 };
                res = LLVMBuildGEP2(ctx->builder, base_ty, op1, indices, 1, "array_gep");
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
        case ALIR_OP_NOT: res = LLVMBuildNot(ctx->builder, op1, "not"); break;
        
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
            
            // Prefer querying the LLVM module directly to avoid pointer-hashmap key issues
            if (inst->op1 && inst->op1->str_val) {
                func = LLVMGetNamedFunction(ctx->llvm_mod, inst->op1->str_val);
                if (func) func_ty = LLVMGlobalGetValueType(func);
            }

            // If not found (e.g., dynamically referenced or external we didn't declare yet)
            if (!func && inst->op1 && inst->op1->str_val) {
                func = hashmap_get(&ctx->func_map, inst->op1->str_val);
                func_ty = hashmap_get(&ctx->func_type_map, inst->op1->str_val);
                
                // If it's still missing, it's an undeclared external function (e.g. standard library). 
                // Let's create an implicit declaration on the fly so we don't segfault.
                if (!func || !func_ty) {
                    LLVMTypeRef ret_ty = inst->dest ? get_llvm_type(ctx, inst->dest->type) : LLVMVoidTypeInContext(ctx->llvm_ctx);
                    LLVMTypeRef *arg_tys = NULL;
                    if (inst->arg_count > 0) {
                        arg_tys = malloc(sizeof(LLVMTypeRef) * inst->arg_count);
                        for (int i = 0; i < inst->arg_count; i++) {
                            arg_tys[i] = get_llvm_type(ctx, inst->args[i]->type);
                        }
                    }
                    // Mark as variadic just in case (e.g. for printf)
                    func_ty = LLVMFunctionType(ret_ty, arg_tys, inst->arg_count, 1);
                    func = LLVMAddFunction(ctx->llvm_mod, inst->op1->str_val, func_ty);
                    if (arg_tys) free(arg_tys);
                }
            }

            if (!func || !func_ty) {
                fprintf(stderr, "Code-Gen Error: Unresolvable function call '%s'\n", inst->op1 ? inst->op1->str_val : "null");
                break; // Skip to prevent crash
            }
            
            LLVMValueRef *args = NULL;
            if (inst->arg_count > 0) {
                args = malloc(sizeof(LLVMValueRef) * inst->arg_count);
                for(int i = 0; i < inst->arg_count; i++) {
                    args[i] = get_llvm_value(ctx, inst->args[i]);
                    // If an argument fails to evaluate, use a dummy value so LLVMBuildCall2 doesn't segfault.
                    if (!args[i]) {
                        LLVMTypeRef arg_ty = get_llvm_type(ctx, inst->args[i]->type);
                        args[i] = LLVMConstNull(arg_ty);
                    }
                }
            }
            
            // LLVM rigorously enforces that void functions cannot be assigned a name.
            LLVMTypeRef ret_ty = LLVMGetReturnType(func_ty);
            int is_void = (ret_ty == LLVMVoidTypeInContext(ctx->llvm_ctx));
            const char* call_name = (inst->dest && !is_void) ? "call" : "";

            res = LLVMBuildCall2(ctx->builder, func_ty, func, args, inst->arg_count, call_name);
            
            if (args) free(args);
            break;
        }
        case ALIR_OP_RET: {
            if (op1) {
                // Ensure we aren't returning a value from a void function
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
            if (op1) res = LLVMBuildBitCast(ctx->builder, op1, get_llvm_type(ctx, inst->dest->type), "bitcast");
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
                LLVMTypeRef i8_ty = LLVMInt8TypeInContext(ctx->llvm_ctx);
                res = LLVMBuildArrayMalloc(ctx->builder, i8_ty, op1, "malloc");
            }
            break;
        }
        case ALIR_OP_FREE: {
            if (op1) LLVMBuildFree(ctx->builder, op1);
            break;
        }
        case ALIR_OP_SIZEOF: {
            LLVMTypeRef ty = get_llvm_type(ctx, inst->op1->type); // the operand holds ALIR_VAL_TYPE info
            res = LLVMSizeOf(ty);
            break;
        }
        case ALIR_OP_MOV: {
            res = op1; // Assign value directly (Register Aliasing)
            break;
        }

        // Catch explicitly untranslated features or flux opcodes mapped out by ALIR
        default: break; 
    }

    if (inst->dest && res) {
        set_llvm_value(ctx, inst->dest, res);
    }
}

LLVMModuleRef codegen_generate(CodegenCtx *ctx) {
    // 1. Pre-declare Structs (Opaque pass to resolve cross references)
    AlirStruct *st = ctx->alir_mod->structs;
    while (st) {
        LLVMTypeRef struct_ty = LLVMStructCreateNamed(ctx->llvm_ctx, st->name);
        hashmap_put(&ctx->struct_map, st->name, struct_ty);
        st = st->next;
    }

    // 1.5. Populate Struct Bodies
    st = ctx->alir_mod->structs;
    while (st) {
        if (st->field_count > 0) {
            LLVMTypeRef *field_tys = malloc(sizeof(LLVMTypeRef) * st->field_count);
            AlirField *f = st->fields;
            while(f) {
                field_tys[f->index] = get_llvm_type(ctx, f->type);
                f = f->next;
            }
            LLVMTypeRef struct_ty = hashmap_get(&ctx->struct_map, st->name);
            LLVMStructSetBody(struct_ty, field_tys, st->field_count, 0);
            free(field_tys);
        }
        st = st->next;
    }

    // 2. Global Strings / Variables
    AlirGlobal *g = ctx->alir_mod->globals;
    while (g) {
        if (g->string_content) {
            LLVMValueRef init_str = LLVMConstStringInContext(ctx->llvm_ctx, g->string_content, strlen(g->string_content), 1);
            LLVMTypeRef str_ty = LLVMTypeOf(init_str);
            LLVMValueRef global_var = LLVMAddGlobal(ctx->llvm_mod, str_ty, g->name);
            LLVMSetInitializer(global_var, init_str);
            LLVMSetLinkage(global_var, LLVMPrivateLinkage);
            LLVMSetGlobalConstant(global_var, 1);
        }
        g = g->next;
    }

    // 3. Function Prototypes (Declarations)
    AlirFunction *func = ctx->alir_mod->functions;
    while (func) {
        LLVMTypeRef ret_ty = get_llvm_type(ctx, func->ret_type);
        LLVMTypeRef *param_tys = NULL;
        
        if (func->param_count > 0) {
            param_tys = malloc(sizeof(LLVMTypeRef) * func->param_count);
            AlirParam *p = func->params;
            int i = 0;
            while(p) {
                param_tys[i++] = get_llvm_type(ctx, p->type);
                p = p->next;
            }
        }
        
        LLVMTypeRef func_ty = LLVMFunctionType(ret_ty, param_tys, func->param_count, 0);
        LLVMValueRef llvm_func = LLVMAddFunction(ctx->llvm_mod, func->name, func_ty);
        
        hashmap_put(&ctx->func_map, func->name, llvm_func);
        hashmap_put(&ctx->func_type_map, func->name, func_ty);
        
        if (param_tys) free(param_tys);
        func = func->next;
    }

    // 4. Function Bodies
    func = ctx->alir_mod->functions;
    while (func) {
        if (func->block_count == 0) { func = func->next; continue; }
        
        LLVMValueRef llvm_func = hashmap_get(&ctx->func_map, func->name);
        
        // Scan instructions to find max needed `temps` length
        ctx->max_temps = 0;
        AlirBlock *b = func->blocks;
        while(b) {
            AlirInst *i = b->head;
            while(i) {
                if (i->dest && i->dest->kind == ALIR_VAL_TEMP && i->dest->temp_id >= ctx->max_temps) {
                    ctx->max_temps = i->dest->temp_id + 1;
                }
                i = i->next;
            }
            b = b->next;
        }
        
        ctx->temps = calloc(ctx->max_temps, sizeof(LLVMValueRef));
        
        // Map native parameter locals to value map (e.g. `p0`, `p1` injected by ALIR generator)
        AlirParam *p = func->params;
        int p_idx = 0;
        while(p) {
            char pname[16]; snprintf(pname, sizeof(pname), "p%d", p_idx);
            LLVMValueRef param_val = LLVMGetParam(llvm_func, p_idx);
            hashmap_put(&ctx->value_map, pname, param_val);
            p_idx++;
            p = p->next;
        }
        
        // Create Basic Blocks
        b = func->blocks;
        while(b) {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(ctx->llvm_ctx, llvm_func, b->label);
            hashmap_put(&ctx->block_map, b->label, bb);
            b = b->next;
        }
        
        // Evaluate Instructions
        b = func->blocks;
        while(b) {
            LLVMBasicBlockRef bb = hashmap_get(&ctx->block_map, b->label);
            LLVMPositionBuilderAtEnd(ctx->builder, bb);
            
            AlirInst *inst = b->head;
            while(inst) {
                translate_inst(ctx, inst);
                inst = inst->next;
            }
            
            // Safety Net: Terminate Basic Block if implicit
            if (!LLVMGetBasicBlockTerminator(bb)) {
                if (func->ret_type.base == TYPE_VOID) {
                    LLVMBuildRetVoid(ctx->builder);
                } else {
                    LLVMBuildUnreachable(ctx->builder);
                }
            }
            
            b = b->next;
        }
        
        free(ctx->temps);
        ctx->temps = NULL;
        func = func->next;
    }
    
    // Verify Module Integrity Check (Optional safety)
    char *err_msg = NULL;
    LLVMVerifyModule(ctx->llvm_mod, LLVMPrintMessageAction, &err_msg);
    if (err_msg) {
        LLVMDisposeMessage(err_msg);
    }

    return ctx->llvm_mod;
}

void codegen_emit_to_file(CodegenCtx *ctx, const char *filename) {
    if (!ctx || !ctx->llvm_mod) return;
    char *err_msg = NULL;
    LLVMPrintModuleToFile(ctx->llvm_mod, filename, &err_msg);
    if (err_msg) {
        fprintf(stderr, "LLVM File Emission Error: %s\n", err_msg);
        LLVMDisposeMessage(err_msg);
    }
}

void codegen_print(CodegenCtx *ctx) {
    if (!ctx || !ctx->llvm_mod) return;
    char *ir = LLVMPrintModuleToString(ctx->llvm_mod);
    printf("%s", ir);
    LLVMDisposeMessage(ir);
}
