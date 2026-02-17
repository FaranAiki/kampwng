#include "codegen.h"

void collect_flux_variables(CodegenCtx *ctx, ASTNode *node, int *struct_idx) {
    if (!node) return;
    
    if (node->type == NODE_VAR_DECL) {
        VarDeclNode *vd = (VarDeclNode*)node;
        FluxVar *fv = malloc(sizeof(FluxVar));
        fv->name = strdup(vd->name);
        fv->vtype = vd->var_type;
        
        if (vd->is_array) {
             LLVMTypeRef et = get_llvm_type(ctx, vd->var_type);
             unsigned int sz = 10;
             if (vd->array_size && vd->array_size->type == NODE_LITERAL) sz = ((LiteralNode*)vd->array_size)->val.int_val;
             fv->type = LLVMArrayType(et, sz);
             fv->vtype.array_size = sz;
        } else {
             if (vd->var_type.base == TYPE_AUTO && vd->initializer) {
                 fv->vtype = codegen_calc_type(ctx, vd->initializer);
                 fv->type = get_llvm_type(ctx, fv->vtype);
             } else {
                 fv->type = get_llvm_type(ctx, vd->var_type);
             }
        }
        
        fv->struct_index = (*struct_idx)++;
        fv->next = ctx->flux_vars;
        ctx->flux_vars = fv;
    }
    
    if (node->type == NODE_IF) {
        collect_flux_variables(ctx, ((IfNode*)node)->then_body, struct_idx);
        collect_flux_variables(ctx, ((IfNode*)node)->else_body, struct_idx);
    } else if (node->type == NODE_LOOP) {
        collect_flux_variables(ctx, ((LoopNode*)node)->body, struct_idx);
    } else if (node->type == NODE_WHILE) {
        collect_flux_variables(ctx, ((WhileNode*)node)->body, struct_idx);
    } else if (node->type == NODE_SWITCH) {
         ASTNode *c = ((SwitchNode*)node)->cases;
         while(c) {
             collect_flux_variables(ctx, ((CaseNode*)c)->body, struct_idx);
             c = c->next;
         }
         collect_flux_variables(ctx, ((SwitchNode*)node)->default_case, struct_idx);
    } else if (node->type == NODE_FOR_IN) {
        ForInNode *fin = (ForInNode*)node;
        FluxVar *fv = malloc(sizeof(FluxVar));
        fv->name = strdup(fin->var_name);
        fv->vtype = fin->iter_type;
        fv->type = get_llvm_type(ctx, fin->iter_type);
        fv->struct_index = (*struct_idx)++;
        fv->next = ctx->flux_vars;
        ctx->flux_vars = fv;
        collect_flux_variables(ctx, fin->body, struct_idx);
    }
    
    collect_flux_variables(ctx, node->next, struct_idx);
}

void codegen_flux_def(CodegenCtx *ctx, FuncDefNode *node) {
    const char *func_name = node->mangled_name ? node->mangled_name : node->name;
    
    // --- STEP 1: PREPARE CONTEXT STRUCT ---
    
    ctx->flux_vars = NULL;
    int struct_idx = 3; 
    
    int param_count = 0;
    Parameter *p = node->params;
    while(p) { param_count++; p = p->next; }
    int total_params = param_count;
    if (node->class_name) total_params++; 
    
    int start_locals_idx = struct_idx + total_params;
    int current_idx = start_locals_idx;
    collect_flux_variables(ctx, node->body, &current_idx);
    
    int field_count = current_idx;
    LLVMTypeRef *elem_types = malloc(sizeof(LLVMTypeRef) * field_count);
    
    elem_types[0] = LLVMInt32Type(); // state
    elem_types[1] = LLVMInt1Type();  // finished
    elem_types[2] = get_llvm_type(ctx, node->ret_type); // yield result
    
    int p_idx = 3;
    if (node->class_name) {
        ClassInfo *ci = find_class(ctx, node->class_name);
        elem_types[p_idx++] = LLVMPointerType(ci->struct_type, 0);
    }
    p = node->params;
    while(p) {
        elem_types[p_idx++] = get_llvm_type(ctx, p->type);
        p = p->next;
    }
    
    FluxVar *fv = ctx->flux_vars;
    while(fv) {
        elem_types[fv->struct_index] = fv->type;
        fv = fv->next;
    }
    
    char struct_name[256];
    snprintf(struct_name, 256, "FluxCtx_%s", func_name);
    LLVMTypeRef ctx_struct_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), struct_name);
    LLVMStructSetBody(ctx_struct_type, elem_types, field_count, false);
    free(elem_types);
    
    // --- STEP 2: GENERATE INIT FUNCTION ---
    LLVMTypeRef *init_param_types = malloc(sizeof(LLVMTypeRef) * total_params);
    p_idx = 0;
    if (node->class_name) {
         ClassInfo *ci = find_class(ctx, node->class_name);
         init_param_types[p_idx++] = LLVMPointerType(ci->struct_type, 0);
    }
    p = node->params;
    while(p) {
        init_param_types[p_idx++] = get_llvm_type(ctx, p->type);
        p = p->next;
    }
    
    LLVMTypeRef init_func_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), init_param_types, total_params, false);
    LLVMValueRef init_func = LLVMAddFunction(ctx->module, func_name, init_func_type);
    
    LLVMBasicBlockRef init_entry = LLVMAppendBasicBlock(init_func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, init_entry);
    
    LLVMValueRef size = LLVMSizeOf(ctx_struct_type);
    LLVMValueRef mem = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "ctx_mem");
    LLVMValueRef ctx_ptr = LLVMBuildBitCast(ctx->builder, mem, LLVMPointerType(ctx_struct_type, 0), "ctx_ptr");
    
    LLVMValueRef state_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx_ptr, 0, "state_ptr");
    LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt32Type(), 0, 0), state_ptr);
    
    LLVMValueRef fin_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx_ptr, 1, "fin_ptr");
    LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt1Type(), 0, 0), fin_ptr);
    
    p_idx = 3;
    if (node->class_name) {
        LLVMValueRef this_val = LLVMGetParam(init_func, 0);
        LLVMValueRef p_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx_ptr, p_idx++, "this_store");
        LLVMBuildStore(ctx->builder, this_val, p_ptr);
    }
    p = node->params;
    int arg_offset = node->class_name ? 1 : 0;
    while(p) {
        LLVMValueRef arg_val = LLVMGetParam(init_func, arg_offset++);
        LLVMValueRef p_ptr = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx_ptr, p_idx++, "param_store");
        LLVMBuildStore(ctx->builder, arg_val, p_ptr);
        p = p->next;
    }
    
    LLVMBuildRet(ctx->builder, mem);
    free(init_param_types);
    
    // --- STEP 3: GENERATE RESUME FUNCTION ---
    char resume_name[512];
    sprintf(resume_name, "%s_Resume", func_name);
    
    LLVMTypeRef resume_arg_types[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef resume_func_type = LLVMFunctionType(LLVMVoidType(), resume_arg_types, 1, false);
    LLVMValueRef resume_func = LLVMAddFunction(ctx->module, resume_name, resume_func_type);
    
    LLVMBasicBlockRef res_entry = LLVMAppendBasicBlock(resume_func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, res_entry);
    
    LLVMValueRef void_ctx = LLVMGetParam(resume_func, 0);
    ctx->flux_ctx_ptr = LLVMBuildBitCast(ctx->builder, void_ctx, LLVMPointerType(ctx_struct_type, 0), "ctx");
    ctx->flux_struct_type = ctx_struct_type;
    
    LLVMValueRef state_addr = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx->flux_ctx_ptr, 0, "state_addr");
    LLVMValueRef current_state = LLVMBuildLoad2(ctx->builder, LLVMInt32Type(), state_addr, "state");
    
    Symbol *saved_scope = ctx->symbols;
    
    p_idx = 3;
    if (node->class_name) {
        LLVMValueRef p_gep = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx->flux_ctx_ptr, p_idx++, "this");
        VarType this_vt = {TYPE_CLASS, 1, strdup(node->class_name)};
        add_symbol(ctx, "this", p_gep, LLVMPointerType(find_class(ctx, node->class_name)->struct_type, 0), this_vt, 0, 0);
    }
    p = node->params;
    while(p) {
        LLVMValueRef p_gep = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx->flux_ctx_ptr, p_idx++, p->name);
        add_symbol(ctx, p->name, p_gep, get_llvm_type(ctx, p->type), p->type, 0, 1);
        p = p->next;
    }
    
    FluxVar *fv_iter = ctx->flux_vars;
    while(fv_iter) {
        LLVMValueRef l_gep = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx->flux_ctx_ptr, fv_iter->struct_index, fv_iter->name);
        add_symbol(ctx, fv_iter->name, l_gep, fv_iter->type, fv_iter->vtype, fv_iter->vtype.array_size > 0, 1);
        fv_iter = fv_iter->next;
    }

    // TERMINATOR MUST BE LAST IN BLOCK
    LLVMBasicBlockRef start_bb = LLVMAppendBasicBlock(resume_func, "start");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlock(resume_func, "end_flux"); 
    
    ctx->flux_resume_switch = LLVMBuildSwitch(ctx->builder, current_state, end_bb, 10);
    LLVMAddCase(ctx->flux_resume_switch, LLVMConstInt(LLVMInt32Type(), 0, 0), start_bb);
    
    LLVMPositionBuilderAtEnd(ctx->builder, start_bb);
    ctx->in_flux_resume = 1;
    ctx->flux_yield_count = 1; 
    
    codegen_node(ctx, node->body);
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMValueRef fin_addr = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, ctx->flux_ctx_ptr, 1, "fin_addr");
        LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt1Type(), 1, 0), fin_addr);
        LLVMBuildRetVoid(ctx->builder);
    }
    
    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    LLVMBuildRetVoid(ctx->builder);
    
    ctx->symbols = saved_scope;
    ctx->in_flux_resume = 0;
    ctx->flux_vars = NULL; 
    ctx->flux_struct_type = NULL;
    ctx->flux_ctx_ptr = NULL;
    ctx->flux_resume_switch = NULL;
}

void codegen_emit(CodegenCtx *ctx, EmitNode *node) {
    if (!ctx->in_flux_resume) {
        codegen_error(ctx, (ASTNode*)node, "emit used outside of flux context");
    }

    LLVMValueRef val = codegen_expr(ctx, node->value);
    LLVMValueRef res_ptr = LLVMBuildStructGEP2(ctx->builder, ctx->flux_struct_type, ctx->flux_ctx_ptr, 2, "res_ptr");
    LLVMBuildStore(ctx->builder, val, res_ptr);
    
    int next_state = ctx->flux_yield_count++;
    LLVMValueRef state_ptr = LLVMBuildStructGEP2(ctx->builder, ctx->flux_struct_type, ctx->flux_ctx_ptr, 0, "state_ptr");
    LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt32Type(), next_state, 0), state_ptr);
    
    LLVMBuildRetVoid(ctx->builder);
    
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    char bb_name[32]; sprintf(bb_name, "resume_%d", next_state);
    LLVMBasicBlockRef resume_bb = LLVMAppendBasicBlock(func, bb_name);
    
    LLVMAddCase(ctx->flux_resume_switch, LLVMConstInt(LLVMInt32Type(), next_state, 0), resume_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, resume_bb);
}

void codegen_for_in(CodegenCtx *ctx, ForInNode *node) {
    LLVMValueRef col = codegen_expr(ctx, node->collection);
    VarType col_type = codegen_calc_type(ctx, node->collection);
    
    int is_flux = 0;
    
    // Explicitly check for flux function call first
    if (node->collection->type == NODE_CALL) {
        CallNode *cn = (CallNode*)node->collection;
        const char *fname = cn->mangled_name ? cn->mangled_name : cn->name;
        FuncSymbol *fs = find_func_symbol(ctx, fname);
        if (fs && fs->is_flux) {
            is_flux = 1;
        }
    }
    
    // Fallback to type check if not explicitly a flux call
    if (!is_flux) {
        if (col_type.base == TYPE_STRING || 
           (col_type.base == TYPE_CHAR && col_type.ptr_depth == 1) || 
           (col_type.base == TYPE_INT && col_type.array_size == 0 && col_type.ptr_depth == 0)) {
            is_flux = 0;
        } else {
            // Assume Flux for anything else (e.g. unknown handles)
            is_flux = 1;
        }
    }
    
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "for_cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "for_body");
    LLVMBasicBlockRef step_bb = LLVMAppendBasicBlock(func, "for_step");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlock(func, "for_end");
    
    LLVMValueRef iter_ptr = NULL;
    
    if (!is_flux) {
        // FIX: Lift alloca to entry block
        LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(func);
        LLVMBuilderRef tmp_b = LLVMCreateBuilder();
        LLVMValueRef first = LLVMGetFirstInstruction(entry_bb);
        if (first) LLVMPositionBuilderBefore(tmp_b, first);
        else LLVMPositionBuilderAtEnd(tmp_b, entry_bb);

        if (col_type.base == TYPE_INT) {
            iter_ptr = LLVMBuildAlloca(tmp_b, LLVMInt64Type(), "range_i");
            LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt64Type(), 0, 0), iter_ptr);
        } else {
            iter_ptr = LLVMBuildAlloca(tmp_b, LLVMPointerType(LLVMInt8Type(), 0), "str_iter");
            LLVMBuildStore(ctx->builder, col, iter_ptr);
        }
        LLVMDisposeBuilder(tmp_b);
    }
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        LLVMBuildBr(ctx->builder, cond_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    
    LLVMValueRef current_val = NULL;
    LLVMValueRef condition = NULL;
    VarType yield_vt = node->iter_type; 

    if (is_flux) {
        char resume_name[512];
        const char *fname_for_struct = "unknown";

        if (node->collection->type == NODE_CALL) {
            CallNode *cn = (CallNode*)node->collection;
            const char *fname = cn->mangled_name ? cn->mangled_name : cn->name;
            sprintf(resume_name, "%s_Resume", fname);
            fname_for_struct = fname;
            
            FuncSymbol *fs = find_func_symbol(ctx, fname);
            if (fs && fs->is_flux) {
                yield_vt = fs->yield_type;
            }
        } else {
            codegen_error(ctx, (ASTNode*)node, "Direct iteration only supported on generator calls for now");
        }
        
        LLVMValueRef resume_func = LLVMGetNamedFunction(ctx->module, resume_name);
        if (!resume_func) {
            char msg[512]; sprintf(msg, "Resume function '%s' not found", resume_name);
            codegen_error(ctx, (ASTNode*)node, msg);
        }

        LLVMValueRef void_ctx = LLVMBuildBitCast(ctx->builder, col, LLVMPointerType(LLVMInt8Type(), 0), "ctx_void");
        LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(resume_func), resume_func, &void_ctx, 1, "");
        
        LLVMTypeRef llvm_yield_type = get_llvm_type(ctx, yield_vt);
        
        // Try to find the exact named struct for perfect alignment
        char struct_name[512];
        snprintf(struct_name, 512, "FluxCtx_%s", fname_for_struct);
        LLVMTypeRef ctx_struct_type = LLVMGetTypeByName(ctx->module, struct_name);

        if (!ctx_struct_type) {
            LLVMTypeRef struct_elems[] = { LLVMInt32Type(), LLVMInt1Type(), llvm_yield_type };
            ctx_struct_type = LLVMStructType(struct_elems, 3, false);
        }
        
        LLVMValueRef typed_ctx = LLVMBuildBitCast(ctx->builder, col, LLVMPointerType(ctx_struct_type, 0), "typed_ctx");
        
        LLVMValueRef fin_addr = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, typed_ctx, 1, "fin_addr");
        LLVMValueRef finished = LLVMBuildLoad2(ctx->builder, LLVMInt1Type(), fin_addr, "finished");
        
        condition = LLVMBuildNot(ctx->builder, finished, "cont");
        
        LLVMValueRef val_addr = LLVMBuildStructGEP2(ctx->builder, ctx_struct_type, typed_ctx, 2, "val_addr");
        current_val = LLVMBuildLoad2(ctx->builder, llvm_yield_type, val_addr, "val");
        
    } else {
        // Fix for when parser didn't set iter_type (which is common for string/range loops)
        if (yield_vt.base == TYPE_VOID || yield_vt.base == TYPE_UNKNOWN) {
             if (col_type.base == TYPE_INT) {
                 yield_vt = (VarType){TYPE_INT, 0, NULL, 0, 0};
             } else {
                 yield_vt = (VarType){TYPE_CHAR, 0, NULL, 0, 0};
             }
        }

        if (col_type.base == TYPE_INT) {
            LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), iter_ptr, "idx");
            LLVMValueRef limit = LLVMBuildIntCast(ctx->builder, col, LLVMInt64Type(), "limit");
            condition = LLVMBuildICmp(ctx->builder, LLVMIntSLT, idx, limit, "chk");
            current_val = LLVMBuildIntCast(ctx->builder, idx, LLVMInt32Type(), "val");
        } else {
            LLVMValueRef p = LLVMBuildLoad2(ctx->builder, LLVMPointerType(LLVMInt8Type(), 0), iter_ptr, "p");
            LLVMValueRef c = LLVMBuildLoad2(ctx->builder, LLVMInt8Type(), p, "char");
            condition = LLVMBuildICmp(ctx->builder, LLVMIntNE, c, LLVMConstInt(LLVMInt8Type(), 0, 0), "chk");
            current_val = c;
        }
    }
    
    LLVMBuildCondBr(ctx->builder, condition, body_bb, end_bb);
    
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    
    // --- ASSIGN LOOP VARIABLE ---
    // If in flux resume, reuse the GEP pointer mapped to struct to avoid stack alloca.
    LLVMValueRef var_ptr = NULL;
    LLVMTypeRef var_type = get_llvm_type(ctx, yield_vt);

    if (ctx->in_flux_resume) {
        Symbol *sym = find_symbol(ctx, node->var_name);
        if (sym) {
            var_ptr = sym->value; 
        }
    }
    
    // Fallback: allocate new if not found or not in flux
    if (!var_ptr) {
        // FIX: Lift alloca to entry block to prevent stack overflow in loops!
        LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(func);
        LLVMBuilderRef tmp_b = LLVMCreateBuilder();
        LLVMValueRef first = LLVMGetFirstInstruction(entry_bb);
        if (first) LLVMPositionBuilderBefore(tmp_b, first);
        else LLVMPositionBuilderAtEnd(tmp_b, entry_bb);

        var_ptr = LLVMBuildAlloca(tmp_b, var_type, node->var_name);
        LLVMDisposeBuilder(tmp_b);
        
        if (!ctx->in_flux_resume) {
             add_symbol(ctx, node->var_name, var_ptr, var_type, yield_vt, 0, 0);
        }
    }
    
    LLVMBuildStore(ctx->builder, current_val, var_ptr);
    
    Symbol *saved_syms = ctx->symbols;
    
    push_loop_ctx(ctx, step_bb, end_bb);
    codegen_node(ctx, node->body);
    pop_loop_ctx(ctx);
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, step_bb);
    }
    
    LLVMPositionBuilderAtEnd(ctx->builder, step_bb);
    if (!is_flux) {
        if (col_type.base == TYPE_INT) {
            LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), iter_ptr, "idx");
            LLVMValueRef nxt = LLVMBuildAdd(ctx->builder, idx, LLVMConstInt(LLVMInt64Type(), 1, 0), "inc");
            LLVMBuildStore(ctx->builder, nxt, iter_ptr);
        } else {
            LLVMValueRef p = LLVMBuildLoad2(ctx->builder, LLVMPointerType(LLVMInt8Type(), 0), iter_ptr, "p");
            LLVMValueRef nxt = LLVMBuildGEP2(ctx->builder, LLVMInt8Type(), p, (LLVMValueRef[]){LLVMConstInt(LLVMInt64Type(), 1, 0)}, 1, "inc");
            LLVMBuildStore(ctx->builder, nxt, iter_ptr);
        }
    }
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, cond_bb);
    }
    
    ctx->symbols = saved_syms;
    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    
    if (is_flux) {
        LLVMValueRef void_ctx = LLVMBuildBitCast(ctx->builder, col, LLVMPointerType(LLVMInt8Type(), 0), "ctx_void");
        LLVMValueRef free_args[] = { void_ctx };
        LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->free_func), ctx->free_func, free_args, 1, "");
    }
}

