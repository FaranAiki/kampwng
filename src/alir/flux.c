#include "alir.h"

static int is_terminator_op(AlirOpcode op) {
    return op == ALIR_OP_RET || op == ALIR_OP_JUMP || op == ALIR_OP_CONDI || op == ALIR_OP_SWITCH || op == ALIR_OP_YIELD;
}

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
        fv->type = fn->iter_type; 
        if (fv->type.base == TYPE_AUTO) fv->type = (VarType){TYPE_INT}; 
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


void alir_gen_flux_def(AlirCtx *ctx, FuncDefNode *fn, const char *class_name) {
    char func_name[512];
    if (class_name) {
        snprintf(func_name, sizeof(func_name), "%s_%s", class_name, fn->name);
    } else {
        snprintf(func_name, sizeof(func_name), "%s", fn->name);
    }

    // 1. Collect Variables to Capture
    ctx->flux_vars = NULL;
    int struct_idx = 4; // Shifted up 1 to accommodate the resume func pointer
    int param_count = 0;
    Parameter *p = fn->params;
    while(p) { param_count++; p = p->next; }
    
    int start_locals = struct_idx + param_count + (class_name ? 1 : 0);
    int current_idx = start_locals;
    
    collect_flux_vars_recursive(ctx, fn->body, &current_idx);
    
    // 2. Register Context Struct
    char struct_name[1024];
    snprintf(struct_name, sizeof(struct_name), "FluxCtx_%s", func_name);
    
    AlirField *fields = NULL;
    AlirField **tail = &fields;
    
    { AlirField *f = alir_alloc(ctx->module, sizeof(AlirField)); f->name = alir_strdup(ctx->module, "state"); f->type = (VarType){TYPE_INT}; f->index=0; *tail=f; tail=&f->next; }
    { AlirField *f = alir_alloc(ctx->module, sizeof(AlirField)); f->name = alir_strdup(ctx->module, "finished"); f->type = (VarType){TYPE_BOOL}; f->index=1; *tail=f; tail=&f->next; }
    { AlirField *f = alir_alloc(ctx->module, sizeof(AlirField)); f->name = alir_strdup(ctx->module, "result"); f->type = fn->ret_type; f->index=2; *tail=f; tail=&f->next; }
    { AlirField *f = alir_alloc(ctx->module, sizeof(AlirField)); f->name = alir_strdup(ctx->module, "resume_func"); f->type = (VarType){TYPE_VOID, 1}; f->index=3; *tail=f; tail=&f->next; } // Map Resume Pointer natively
    
    int p_idx = 4;
    if (class_name) {
        AlirField *f = alir_alloc(ctx->module, sizeof(AlirField));
        f->name = alir_strdup(ctx->module, "this");
        f->type = (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, class_name)}; 
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
    VarType ret_type = {TYPE_CLASS, 1, 0, alir_strdup(ctx->module, struct_name)};
    ctx->current_func = alir_add_function(ctx->module, func_name, ret_type, 0); 
    
    if (class_name) alir_func_add_param(ctx->module, ctx->current_func, "this", (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, class_name)});
    p = fn->params;
    while(p) {
        alir_func_add_param(ctx->module, ctx->current_func, p->name, p->type);
        p = p->next;
    }
    
    ctx->current_block = alir_add_block(ctx->module, ctx->current_func, "entry");
    
    AlirValue *size_val = new_temp(ctx, (VarType){TYPE_INT});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_SIZEOF, size_val, alir_val_type(ctx->module, struct_name), NULL));
    
    AlirValue *raw_mem = new_temp(ctx, (VarType){TYPE_CHAR, 1});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOC_HEAP, raw_mem, size_val, NULL));
    
    AlirValue *ctx_ptr = new_temp(ctx, ret_type);
    emit(ctx, mk_inst(ctx->module, ALIR_OP_BITCAST, ctx_ptr, raw_mem, NULL));
    
    AlirValue *ptr_state = new_temp(ctx, (VarType){TYPE_INT, 1});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr_state, ctx_ptr, alir_const_int(ctx->module, 0)));
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 0), ptr_state));
    
    AlirValue *ptr_fin = new_temp(ctx, (VarType){TYPE_BOOL, 1});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr_fin, ctx_ptr, alir_const_int(ctx->module, 1)));
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 0), ptr_fin));
    
    char resume_name[1024]; snprintf(resume_name, sizeof(resume_name), "%s_Resume", func_name);
    AlirValue *resume_val = alir_val_var(ctx->module, resume_name);
    resume_val->type = (VarType){TYPE_VOID, 1}; // Function pointer

    AlirValue *ptr_res = new_temp(ctx, (VarType){TYPE_VOID, 2}); 
    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr_res, ctx_ptr, alir_const_int(ctx->module, 3)));
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, resume_val, ptr_res)); // Bind Resume to the Struct Call
    
    int param_offset = 0;
    p_idx = 4;
    if (class_name) {
        char arg_name[16]; sprintf(arg_name, "p%d", param_offset++);
        AlirValue *arg_val = alir_val_var(ctx->module, arg_name); 
        arg_val->type = (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, class_name)}; 
        
        AlirValue *f_ptr = new_temp(ctx, (VarType){TYPE_CLASS, 2, 0, alir_strdup(ctx->module, class_name)});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, f_ptr, ctx_ptr, alir_const_int(ctx->module, p_idx++)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, arg_val, f_ptr));
    }
    p = fn->params;
    while(p) {
        char arg_name[16]; sprintf(arg_name, "p%d", param_offset++);
        AlirValue *arg_val = alir_val_var(ctx->module, arg_name);
        arg_val->type = p->type; 
        
        VarType pt = p->type; pt.ptr_depth++;
        AlirValue *f_ptr = new_temp(ctx, pt);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, f_ptr, ctx_ptr, alir_const_int(ctx->module, p_idx++)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, arg_val, f_ptr));
        p = p->next;
    }
    
    emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, ctx_ptr, NULL));
    
    // 4. Generate RESUME Function
    ctx->current_func = alir_add_function(ctx->module, resume_name, (VarType){TYPE_VOID}, 0);
    alir_func_add_param(ctx->module, ctx->current_func, "ctx", ret_type); 
    
    ctx->current_block = alir_add_block(ctx->module, ctx->current_func, "entry");
    
    ctx->in_flux_resume = 1;
    ctx->flux_struct_name = alir_strdup(ctx->module, struct_name);
    ctx->flux_yield_count = 1;
    
    // [CRITICAL FIX] Bind the exact struct type to the parameter to prevent ALIR backend
    // from defaulting to `void ptr` when processing field accesses!
    ctx->flux_ctx_ptr = alir_val_var(ctx->module, "p0"); 
    ctx->flux_ctx_ptr->type = ret_type;
    
    AlirValue *ptr_st = new_temp(ctx, (VarType){TYPE_INT, 1});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr_st, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 0)));
    AlirValue *current_state = new_temp(ctx, (VarType){TYPE_INT});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, current_state, ptr_st, NULL));

    ctx->symbols = NULL; 
    p_idx = 4;
    if (class_name) {
         VarType pt = {TYPE_CLASS, 1, 0, alir_strdup(ctx->module, class_name)}; pt.ptr_depth++;
         AlirValue *ptr = new_temp(ctx, pt);
         emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, p_idx++)));
         alir_add_symbol(ctx, "this", ptr, (VarType){TYPE_CLASS, 1, 0, alir_strdup(ctx->module, class_name)});
    }
    p = fn->params;
    while(p) {
        VarType pt = p->type; pt.ptr_depth++;
        AlirValue *ptr = new_temp(ctx, pt);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, p_idx++)));
        alir_add_symbol(ctx, p->name, ptr, p->type);
        p = p->next;
    }
    
    fv = ctx->flux_vars;
    while(fv) {
        VarType pt = fv->type; pt.ptr_depth++;
        AlirValue *ptr = new_temp(ctx, pt);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, fv->index)));
        alir_add_symbol(ctx, fv->name, ptr, fv->type);
        fv = fv->next;
    }
    
    AlirBlock *start_bb = alir_add_block(ctx->module, ctx->current_func, "flux_start");
    AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "flux_end");
    
    AlirInst *sw = mk_inst(ctx->module, ALIR_OP_SWITCH, NULL, current_state, alir_val_label(ctx->module, end_bb->label));
    ctx->flux_resume_switch = sw;
    
    AlirSwitchCase *c0 = alir_alloc(ctx->module, sizeof(AlirSwitchCase));
    c0->value = 0; c0->label = start_bb->label;
    sw->cases = c0;
    emit(ctx, sw);
    
    ctx->current_block = start_bb;
    
    ASTNode *stmt = fn->body;
    while(stmt) { alir_gen_stmt(ctx, stmt); stmt = stmt->next; }
    
    // Prevent LLVM unreachable exceptions by ONLY dropping a closing ret if the block doesn't naturally terminate via jumps/rets
    if (!ctx->current_block->tail || !is_terminator_op(ctx->current_block->tail->op)) {
        AlirValue *p_fin = new_temp(ctx, (VarType){TYPE_BOOL, 1});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, p_fin, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 1)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 1), p_fin));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
    }
    
    ctx->current_block = end_bb;
    if (!ctx->current_block->tail || !is_terminator_op(ctx->current_block->tail->op)) {
        emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
    }
    
    ctx->in_flux_resume = 0;
    ctx->flux_vars = NULL;
    ctx->flux_resume_switch = NULL;
}

void alir_gen_flux_yield(AlirCtx *ctx, EmitNode *en) {
    if (ctx->in_flux_resume) {
        AlirValue *val = alir_gen_expr(ctx, en->value);
        if (!val) val = alir_const_int(ctx->module, 0); // Safety net
        
        // [CRITICAL FIX] Must be a pointer to the value, not the value type itself
        VarType res_t = val->type; res_t.ptr_depth++;
        
        AlirValue *res_ptr = new_temp(ctx, res_t); 
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, res_ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 2)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, res_ptr));
        
        int next_state = ctx->flux_yield_count++;
        AlirValue *state_ptr = new_temp(ctx, (VarType){TYPE_INT, 1});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, state_ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 0)));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, next_state), state_ptr));
        
        emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
        
        char label[32]; sprintf(label, "resume_%d", next_state);
        AlirBlock *resume_bb = alir_add_block(ctx->module, ctx->current_func, label);
        ctx->current_block = resume_bb;
        
        AlirSwitchCase *nc = alir_alloc(ctx->module, sizeof(AlirSwitchCase));
        nc->value = next_state;
        nc->label = resume_bb->label;
        nc->next = ctx->flux_resume_switch->cases;
        ctx->flux_resume_switch->cases = nc;
        
    } else {
        AlirValue *val = alir_gen_expr(ctx, en->value);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_YIELD, NULL, val, NULL));
    }
}
