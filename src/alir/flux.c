#include "alir.h"

void collect_flux_vars_recursive(AlirCtx *ctx, ASTNode *node, int *idx_ptr) {
    if (!node) return;
    
    if (node->type == NODE_VAR_DECL) {
        VarDeclNode *vn = (VarDeclNode*)node;
        FluxVar *fv = alir_alloc(ctx->module, sizeof(FluxVar));
        fv->name = alir_strdup(ctx->module, vn->name);
        fv->type = vn->var_type;
        fv->index = (*idx_ptr)++;
        fv->next = ctx->flux_vars;
        ctx->flux_vars = fv;
    }
    else if (node->type == NODE_IF) {
        collect_flux_vars_recursive(ctx, ((IfNode*)node)->then_body, idx_ptr);
        collect_flux_vars_recursive(ctx, ((IfNode*)node)->else_body, idx_ptr);
    } 
    else if (node->type == NODE_WHILE) {
        collect_flux_vars_recursive(ctx, ((WhileNode*)node)->body, idx_ptr);
    }
    else if (node->type == NODE_LOOP) {
        collect_flux_vars_recursive(ctx, ((LoopNode*)node)->body, idx_ptr);
    }
    else if (node->type == NODE_FOR_IN) {
        ForInNode *fn = (ForInNode*)node;
        FluxVar *fv = alir_alloc(ctx->module, sizeof(FluxVar));
        fv->name = alir_strdup(ctx->module, fn->var_name);
        fv->type = fn->iter_type; // Usually AUTO, should be resolved
        if (fv->type.base == TYPE_AUTO) fv->type = (VarType){TYPE_INT}; // Fallback if not resolved
        fv->index = (*idx_ptr)++;
        fv->next = ctx->flux_vars;
        ctx->flux_vars = fv;
        collect_flux_vars_recursive(ctx, fn->body, idx_ptr);
    }
    else if (node->type == NODE_SWITCH) {
        ASTNode *c = ((SwitchNode*)node)->cases;
        while(c) {
            collect_flux_vars_recursive(ctx, ((CaseNode*)c)->body, idx_ptr);
            c = c->next;
        }
        collect_flux_vars_recursive(ctx, ((SwitchNode*)node)->default_case, idx_ptr);
    }
    
    collect_flux_vars_recursive(ctx, node->next, idx_ptr);
}


void alir_gen_flux_def(AlirCtx *ctx, FuncDefNode *fn) {
    // 1. Collect Variables to Capture
    ctx->flux_vars = NULL;
    int struct_idx = 3; // 0=state, 1=finished, 2=result
    
    // Count Params
    int param_count = 0;
    Parameter *p = fn->params;
    while(p) { param_count++; p = p->next; }
    
    // Total struct start index for locals = 3 + param_count + (1 if class member)
    int start_locals = struct_idx + param_count + (fn->class_name ? 1 : 0);
    int current_idx = start_locals;
    
    collect_flux_vars_recursive(ctx, fn->body, &current_idx);
    
    // 2. Register Context Struct
    char struct_name[256];
    snprintf(struct_name, 256, "FluxCtx_%s", fn->name);
    
    AlirField *fields = NULL;
    AlirField **tail = &fields;
    
    // Add Header Fields
    // Field 0: state (int)
    { AlirField *f = alir_alloc(ctx->module, sizeof(AlirField)); f->name = alir_strdup(ctx->module, "state"); f->type = (VarType){TYPE_INT}; f->index=0; *tail=f; tail=&f->next; }
    // Field 1: finished (bool)
    { AlirField *f = alir_alloc(ctx->module, sizeof(AlirField)); f->name = alir_strdup(ctx->module, "finished"); f->type = (VarType){TYPE_BOOL}; f->index=1; *tail=f; tail=&f->next; }
    // Field 2: result (ret_type)
    { AlirField *f = alir_alloc(ctx->module, sizeof(AlirField)); f->name = alir_strdup(ctx->module, "result"); f->type = fn->ret_type; f->index=2; *tail=f; tail=&f->next; }
    
    // Add Params
    int p_idx = 3;
    if (fn->class_name) {
        AlirField *f = alir_alloc(ctx->module, sizeof(AlirField));
        f->name = alir_strdup(ctx->module, "this");
        f->type = (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, fn->class_name)}; // Pointer to class
        f->index = p_idx++;
        *tail=f; tail=&f->next;
    }
    p = fn->params;
    while(p) {
        AlirField *f = alir_alloc(ctx->module, sizeof(AlirField));
        f->name = alir_strdup(ctx->module, p->name);
        f->type = p->type;
        f->index = p_idx++;
        *tail=f; tail=&f->next;
        p = p->next;
    }
    
    // Add Locals
    FluxVar *fv = ctx->flux_vars;
    while(fv) {
        AlirField *f = alir_alloc(ctx->module, sizeof(AlirField));
        f->name = alir_strdup(ctx->module, fv->name);
        f->type = fv->type;
        f->index = fv->index;
        *tail=f; tail=&f->next;
        fv = fv->next;
    }
    
    alir_register_struct(ctx->module, struct_name, fields);
    
    // 3. Generate INIT Function (The Generator Factory)
    ctx->current_func = alir_add_function(ctx->module, fn->name, (VarType){TYPE_CHAR, 1}, 0); // Returns char* (opaque ptr)
    // Add params to Init func
    if (fn->class_name) alir_func_add_param(ctx->module, ctx->current_func, "this", (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, fn->class_name)});
    p = fn->params;
    while(p) {
        alir_func_add_param(ctx->module, ctx->current_func, p->name, p->type);
        p = p->next;
    }
    
    ctx->current_block = alir_add_block(ctx->module, ctx->current_func, "entry");
    
    // Allocate Struct
    AlirValue *size_val = new_temp(ctx, (VarType){TYPE_INT});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_SIZEOF, size_val, alir_val_type(ctx->module, struct_name), NULL));
    
    AlirValue *raw_mem = new_temp(ctx, (VarType){TYPE_CHAR, 1});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOC_HEAP, raw_mem, size_val, NULL));
    
    AlirValue *ctx_ptr = new_temp(ctx, (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, struct_name)});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_BITCAST, ctx_ptr, raw_mem, NULL));
    
    // Init Header: state=0, finished=0
    AlirValue *ptr_state = new_temp(ctx, (VarType){TYPE_INT, 1});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr_state, ctx_ptr, alir_const_int(ctx->module, 0)));
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 0), ptr_state));
    
    AlirValue *ptr_fin = new_temp(ctx, (VarType){TYPE_BOOL, 1});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr_fin, ctx_ptr, alir_const_int(ctx->module, 1)));
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 0), ptr_fin));
    
    // Store Params into Struct
    int param_offset = 0;
    p_idx = 3;
    if (fn->class_name) {
        char arg_name[16]; sprintf(arg_name, "p%d", param_offset-1);
        AlirValue *arg_val = alir_val_var(ctx->module, arg_name); // Placeholder for arg value
        arg_val->type = (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, fn->class_name)}; // [FIX] void store patch
        
        AlirValue *f_ptr = new_temp(ctx, (VarType){TYPE_CLASS, 2, 0, alir_strdup(ctx->module, fn->class_name)});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, f_ptr, ctx_ptr, alir_const_int(ctx->module, p_idx++)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, arg_val, f_ptr));
    }
    p = fn->params;
    while(p) {
        char arg_name[16]; sprintf(arg_name, "p%d", param_offset++);
        AlirValue *arg_val = alir_val_var(ctx->module, arg_name);
        arg_val->type = p->type; // [FIX] void store patch
        
        VarType pt = p->type; pt.ptr_depth++;
        AlirValue *f_ptr = new_temp(ctx, pt);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, f_ptr, ctx_ptr, alir_const_int(ctx->module, p_idx++)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, arg_val, f_ptr));
        p = p->next;
    }
    
    // Return Context Ptr
    emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, raw_mem, NULL));
    
    // 4. Generate RESUME Function
    char resume_name[256]; snprintf(resume_name, 256, "%s_Resume", fn->name);
    ctx->current_func = alir_add_function(ctx->module, resume_name, (VarType){TYPE_VOID}, 0);
    alir_func_add_param(ctx->module, ctx->current_func, "ctx", (VarType){TYPE_CHAR, 1}); // void* ctx
    
    ctx->current_block = alir_add_block(ctx->module, ctx->current_func, "entry");
    
    // Prepare Flux Context
    ctx->in_flux_resume = 1;
    ctx->flux_struct_name = alir_strdup(ctx->module, struct_name);
    ctx->flux_yield_count = 1; // State 0 is entry, next is 1
    
    // Bitcast void* ctx to FluxCtx*
    AlirValue *void_ctx = alir_val_var(ctx->module, "p0"); // First arg
    ctx->flux_ctx_ptr = new_temp(ctx, (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, struct_name)});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_BITCAST, ctx->flux_ctx_ptr, void_ctx, NULL));
    
    // Load State
    AlirValue *ptr_st = new_temp(ctx, (VarType){TYPE_INT, 1});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr_st, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 0)));
    AlirValue *current_state = new_temp(ctx, (VarType){TYPE_INT});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, current_state, ptr_st, NULL));
    
    // Create Switch
    AlirBlock *start_bb = alir_add_block(ctx->module, ctx->current_func, "flux_start");
    AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "flux_end");
    
    AlirInst *sw = mk_inst(ctx->module, ALIR_OP_SWITCH, NULL, current_state, alir_val_label(ctx->module, end_bb->label));
    ctx->flux_resume_switch = sw;
    
    // Case 0 -> Start
    AlirSwitchCase *c0 = alir_alloc(ctx->module, sizeof(AlirSwitchCase));
    c0->value = 0; c0->label = start_bb->label;
    sw->cases = c0;
    emit(ctx, sw);
    
    // Populate Symbols for Parameters in Resume
    ctx->current_block = start_bb;
    ctx->symbols = NULL; // Clear symbols from Init func
    
    p_idx = 3;
    if (fn->class_name) {
         VarType pt = {TYPE_CLASS, 1, 0, alir_strdup(ctx->module, fn->class_name)}; pt.ptr_depth++; // Pointer to pointer
         AlirValue *ptr = new_temp(ctx, pt);
         emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, p_idx++)));
         // Deref once to get the 'this' pointer value? 
         // Logic in alir_gen_var_ref does LOAD. So we need the address of the variable.
         // 'this' is stored in the struct. 'ptr' is the address of 'this' in the struct.
         // So adding 'ptr' to symbol table is correct.
         alir_add_symbol(ctx, "this", ptr, (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, fn->class_name)});
    }
    p = fn->params;
    while(p) {
        VarType pt = p->type; pt.ptr_depth++;
        AlirValue *ptr = new_temp(ctx, pt);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, p_idx++)));
        alir_add_symbol(ctx, p->name, ptr, p->type);
        p = p->next;
    }
    
    // Generate Body
    ASTNode *stmt = fn->body;
    while(stmt) { alir_gen_stmt(ctx, stmt); stmt = stmt->next; }
    
    // Default Finish (if fallthrough)
    if (!ctx->current_block->tail || ctx->current_block->tail->op != ALIR_OP_RET) {
        // Set finished=1
        AlirValue *p_fin = new_temp(ctx, (VarType){TYPE_BOOL, 1});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, p_fin, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 1)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 1), p_fin));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
    }
    
    ctx->current_block = end_bb;
    emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
    
    // Cleanup
    ctx->in_flux_resume = 0;
    ctx->flux_vars = NULL;
    ctx->flux_resume_switch = NULL;
}

void alir_gen_flux_yield(AlirCtx *ctx, EmitNode *en) {
    if (ctx->in_flux_resume) {
        // --- FLUX YIELD LOWERING ---
        // 1. Evaluate Value
        AlirValue *val = alir_gen_expr(ctx, en->value);
        
        // 2. Store to Context->Result (Index 2)
        // struct { state, finished, result, ... }
        AlirValue *res_ptr = new_temp(ctx, val->type); // Should correspond to yield type
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, res_ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 2)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, res_ptr));
        
        // 3. Update State
        int next_state = ctx->flux_yield_count++;
        AlirValue *state_ptr = new_temp(ctx, (VarType){TYPE_INT, 1});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, state_ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 0)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, next_state), state_ptr));
        
        // 4. Return Void
        emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
        
        // 5. Create Resume Block for Next State
        char label[32]; sprintf(label, "resume_%d", next_state);
        AlirBlock *resume_bb = alir_add_block(ctx->module, ctx->current_func, label);
        ctx->current_block = resume_bb;
        
        // 6. Patch Switch
        AlirSwitchCase *nc = alir_alloc(ctx->module, sizeof(AlirSwitchCase));
        nc->value = next_state;
        nc->label = resume_bb->label;
        nc->next = ctx->flux_resume_switch->cases;
        ctx->flux_resume_switch->cases = nc;
        
    } else {
        // Fallback for non-lowered yield (if supported directly)
        AlirValue *val = alir_gen_expr(ctx, en->value);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_YIELD, NULL, val, NULL));
    }
}
