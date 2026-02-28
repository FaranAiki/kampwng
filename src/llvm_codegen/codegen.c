#include "../../include/llvm_codegen/codegen.h"
#include "../../include/common/hashmap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

LLVMTypeRef get_llvm_type(CodegenCtx *ctx, VarType t) {
    if (t.ptr_depth > 0) {
        return LLVMPointerType(LLVMInt8TypeInContext(ctx->llvm_ctx), 0);
    }

    LLVMTypeRef base = NULL;
    switch (t.base) {
        case TYPE_VOID: base = LLVMVoidTypeInContext(ctx->llvm_ctx); break;
        case TYPE_INT: base = LLVMInt32TypeInContext(ctx->llvm_ctx); break;
        case TYPE_SHORT: base = LLVMInt16TypeInContext(ctx->llvm_ctx); break;
        case TYPE_LONG: 
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
                base = LLVMInt8TypeInContext(ctx->llvm_ctx); 
            }
            break;
        }
        case TYPE_ENUM: base = LLVMInt32TypeInContext(ctx->llvm_ctx); break;
        default: base = LLVMInt32TypeInContext(ctx->llvm_ctx); break; 
    }

    if (t.array_size > 0) {
        base = LLVMArrayType(base, t.array_size);
    }
    
    return base;
}

void set_llvm_value(CodegenCtx *ctx, AlirValue *v, LLVMValueRef llvm_val) {
    if (!v) return;
    if (v->kind == ALIR_VAL_TEMP) {
        if (v->temp_id < ctx->max_temps) {
            ctx->temps[v->temp_id] = llvm_val;
        }
    } else if (v->kind == ALIR_VAL_VAR) {
        hashmap_put(&ctx->value_map, v->str_val, llvm_val);
    }
}

LLVMValueRef get_llvm_value(CodegenCtx *ctx, AlirValue *v) {
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
            // Fix: The '0' appended at the end requests LLVM to null terminate the string!
            LLVMValueRef init_str = LLVMConstStringInContext(ctx->llvm_ctx, g->string_content, strlen(g->string_content), 0);
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
                VarType p_ty = p->type;
                if (p_ty.array_size > 0) { p_ty.array_size = 0; p_ty.ptr_depth++; } // Parameter decay
                param_tys[i++] = get_llvm_type(ctx, p_ty);
                p = p->next;
            }
        }
        
        LLVMTypeRef func_ty = LLVMFunctionType(ret_ty, param_tys, func->param_count, func->is_varargs);
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
                    printf("unreachable\n");
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
