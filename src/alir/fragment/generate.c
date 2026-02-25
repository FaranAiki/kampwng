#include "alir.h"

int alir_is_integer_type(VarType t) {
    return t.base == TYPE_INT || t.base == TYPE_CHAR || t.base == TYPE_BOOL;
}

void alir_stmt_vardecl(AlirCtx *ctx, ASTNode *node) {
    VarDeclNode *vn = (VarDeclNode*)node;
    int is_array_lit = (vn->initializer && vn->initializer->type == NODE_ARRAY_LIT);

    if (is_array_lit) {
        ArrayLitNode *al = (ArrayLitNode*)vn->initializer;
        int count = 0;
        ASTNode *elem = al->elements;
        while(elem) { count++; elem = elem->next; }
        
        // Find base type
        VarType elem_type = {TYPE_INT, 0, 0, NULL}; 
        if (al->elements) {
            elem_type = sem_get_node_type(ctx->sem, al->elements);
            if (elem_type.base == TYPE_UNKNOWN || elem_type.base == TYPE_AUTO) {
                if (al->elements->type == NODE_LITERAL) {
                    elem_type = ((LiteralNode*)al->elements)->var_type;
                }
            }
        }
        
        vn->var_type.base = elem_type.base;
        vn->var_type.ptr_depth = elem_type.ptr_depth + 1; // It becomes a pointer to the element
        vn->var_type.array_size = 0; // It is NOT an inline stack array anymore
        if (elem_type.class_name) {
            vn->var_type.class_name = alir_strdup(ctx->module, elem_type.class_name);
        }
        
        // [HOTFIX] Allocate on Heap to prevent backend `onstack` size smash bugs
        int byte_size = count * 8; // 8 bytes per element safely overestimates i32/pointers
        AlirValue *size_val = alir_const_int(ctx->module, byte_size);
        AlirValue *raw_ptr = new_temp(ctx, (VarType){TYPE_CHAR, 1});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOC_HEAP, raw_ptr, size_val, NULL));
        
        AlirValue *ptr = new_temp(ctx, vn->var_type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_BITCAST, ptr, raw_ptr, NULL));
        alir_add_symbol(ctx, vn->name, ptr, vn->var_type);
        
        // Store elements sequentially
        elem = al->elements;
        int idx = 0;
        while(elem) {
            AlirValue *eval = alir_gen_expr(ctx, elem);
            AlirValue *elem_ptr = new_temp(ctx, vn->var_type); // Pointer to element
            emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, elem_ptr, ptr, alir_const_int(ctx->module, idx)));
            emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, eval ? eval : alir_const_int(ctx->module, 0), elem_ptr));
            elem = elem->next; idx++;
        }
        return;
    } 

    if (vn->var_type.base == TYPE_CLASS && vn->var_type.ptr_depth == 0) {
        vn->var_type.ptr_depth = 1;
    }
    
    AlirValue *val = NULL;
    if (vn->initializer) {
        val = alir_gen_expr(ctx, vn->initializer);
        if (val) {
            if (vn->var_type.base == TYPE_AUTO || vn->var_type.base == TYPE_UNKNOWN) {
                vn->var_type = val->type;
            } else if (val->type.ptr_depth > vn->var_type.ptr_depth) {
                vn->var_type = val->type;
            } else if (vn->var_type.base == TYPE_CLASS && val->type.base == TYPE_CLASS) {
                vn->var_type.ptr_depth = val->type.ptr_depth;
            }
        }
    }
    
    AlirValue *ptr = new_temp(ctx, vn->var_type);
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
    alir_add_symbol(ctx, vn->name, ptr, vn->var_type);
    
    if (vn->initializer) {
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val ? val : alir_const_int(ctx->module, 0), ptr));
    }
}

void alir_stmt_assign(AlirCtx *ctx, ASTNode *node) {
    AssignNode *an = (AssignNode*)node;
    AlirValue *val = alir_gen_expr(ctx, an->value);
    if (!val) val = alir_const_int(ctx->module, 0);

    AlirValue *ptr = NULL;
    if (an->target) {
        ptr = alir_gen_addr(ctx, an->target);
    } else if (an->name) {
        AlirSymbol *s = alir_find_symbol(ctx, an->name);
        if (s) { ptr = s->ptr; } else { ptr = alir_val_global(ctx->module, an->name, val->type); }
    }
    
    if (!ptr) {
        ptr = new_temp(ctx, val->type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
    }
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
}

void alir_stmt_while(AlirCtx *ctx, ASTNode *node) {
    WhileNode *wn = (WhileNode*)node;
    AlirBlock *cond_bb = alir_add_block(ctx->module, ctx->current_func, "while_cond");
    AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "while_body");
    AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "while_end");

    if (!wn->is_do_while) {
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
    }

    ctx->current_block = cond_bb;
    AlirValue *cond = alir_gen_expr(ctx, wn->condition);
    if (!cond) cond = alir_const_int(ctx->module, 0);

    AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, cond, alir_val_label(ctx->module, body_bb->label));
    br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
    br->args[0] = alir_val_label(ctx->module, end_bb->label);
    br->arg_count = 1;
    emit(ctx, br);

    ctx->current_block = body_bb;
    push_loop(ctx, cond_bb, end_bb);
    ASTNode *s = wn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
    pop_loop(ctx);
    
    if (!ctx->current_block->tail || !is_terminator(ctx->current_block->tail->op)) {
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
    }
    
    ctx->current_block = end_bb;
}

void alir_stmt_for_in(AlirCtx *ctx, ASTNode *node) {
    ForInNode *fn = (ForInNode*)node;
    AlirValue *col = alir_gen_expr(ctx, fn->collection);
    
    // --- 1. GENERATOR LOOP LOWERING ---
    if (col && col->type.base == TYPE_CLASS && col->type.class_name && strncmp(col->type.class_name, "FluxCtx_", 8) == 0) {
        char *flux_func_name = col->type.class_name + 8;
        char resume_name[256]; snprintf(resume_name, 256, "%s_Resume", flux_func_name);
        
        AlirBlock *cond_bb = alir_add_block(ctx->module, ctx->current_func, "for_cond");
        AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "for_body");
        AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "for_end");
        
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
        ctx->current_block = cond_bb;
        
        // Call Resume
        AlirValue *resume_func = alir_val_global(ctx->module, resume_name, (VarType){TYPE_VOID});
        AlirInst *call_resume = mk_inst(ctx->module, ALIR_OP_CALL, NULL, resume_func, NULL);
        call_resume->args = alir_alloc(ctx->module, sizeof(AlirValue*));
        call_resume->args[0] = col;
        call_resume->arg_count = 1;
        emit(ctx, call_resume);
        
        // Check finished (bool at index 1)
        AlirValue *fin_ptr = new_temp(ctx, (VarType){TYPE_BOOL, 1});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, fin_ptr, col, alir_const_int(ctx->module, 1)));
        AlirValue *is_fin = new_temp(ctx, (VarType){TYPE_BOOL});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, is_fin, fin_ptr, NULL));
        
        // valid = (finished == false)
        AlirValue *valid = new_temp(ctx, (VarType){TYPE_BOOL});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_EQ, valid, is_fin, alir_const_int(ctx->module, 0)));
        
        AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, valid, alir_val_label(ctx->module, body_bb->label));
        br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
        br->args[0] = alir_val_label(ctx->module, end_bb->label);
        br->arg_count = 1;
        emit(ctx, br);
        
        ctx->current_block = body_bb;
        push_loop(ctx, cond_bb, end_bb);
        
        // Load result (index 2)
        VarType res_t = fn->iter_type; res_t.ptr_depth++;
        AlirValue *res_ptr = new_temp(ctx, res_t);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, res_ptr, col, alir_const_int(ctx->module, 2)));
        AlirValue *val = new_temp(ctx, fn->iter_type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, val, res_ptr, NULL));
        
        // Store to loop var
        AlirValue *var_ptr = new_temp(ctx, fn->iter_type); 
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, var_ptr, NULL, NULL));
        alir_add_symbol(ctx, fn->var_name, var_ptr, fn->iter_type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, var_ptr));
        
        ASTNode *s = fn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
        
        if (!ctx->current_block->tail || !is_terminator(ctx->current_block->tail->op)) {
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
        }
        pop_loop(ctx);
        ctx->current_block = end_bb;
        return;
    }

    // --- 2. INTEGER RANGE LOOP LOWERING (for i in 50) ---
    if (col && alir_is_integer_type(col->type) && col->type.ptr_depth == 0) {
        AlirValue *limit = col;
        AlirBlock *cond_bb = alir_add_block(ctx->module, ctx->current_func, "for_cond");
        AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "for_body");
        AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "for_end");
        
        AlirValue *var_ptr = new_temp(ctx, fn->iter_type); 
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, var_ptr, NULL, NULL));
        alir_add_symbol(ctx, fn->var_name, var_ptr, fn->iter_type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 0), var_ptr));
        
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
        ctx->current_block = cond_bb;
        
        // i < limit
        AlirValue *i_val = new_temp(ctx, fn->iter_type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, i_val, var_ptr, NULL));
        AlirValue *valid = new_temp(ctx, (VarType){TYPE_BOOL});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_LT, valid, i_val, limit));
        
        AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, valid, alir_val_label(ctx->module, body_bb->label));
        br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
        br->args[0] = alir_val_label(ctx->module, end_bb->label);
        br->arg_count = 1;
        emit(ctx, br);
        
        ctx->current_block = body_bb;
        push_loop(ctx, cond_bb, end_bb);
        
        ASTNode *s = fn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
        
        if (!ctx->current_block->tail || !is_terminator(ctx->current_block->tail->op)) {
            AlirValue *i_curr = new_temp(ctx, fn->iter_type);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, i_curr, var_ptr, NULL));
            AlirValue *i_next = new_temp(ctx, fn->iter_type);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_ADD, i_next, i_curr, alir_const_int(ctx->module, 1)));
            emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, i_next, var_ptr));
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
        }
        pop_loop(ctx);
        ctx->current_block = end_bb;
        return;
    }

    // --- 3. NATIVE ITERATOR FALLBACK ---
    if (!col) {
        col = new_temp(ctx, (VarType){TYPE_AUTO, 1, 0, NULL, 0, 0});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, col, NULL, NULL));
    }
    
    AlirValue *iter = new_temp(ctx, (VarType){TYPE_VOID, 1, 0, NULL, 0, 0}); 
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_INIT, iter, col, NULL));
    
    AlirBlock *cond_bb = alir_add_block(ctx->module, ctx->current_func, "for_cond");
    AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "for_body");
    AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "for_end");
    
    emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
    
    ctx->current_block = cond_bb;
    AlirValue *valid = new_temp(ctx, (VarType){TYPE_BOOL, 0, 0, NULL, 0, 0});
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_VALID, valid, iter, NULL));
    
    AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, valid, alir_val_label(ctx->module, body_bb->label));
    br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
    br->args[0] = alir_val_label(ctx->module, end_bb->label);
    br->arg_count = 1;
    emit(ctx, br);
    
    ctx->current_block = body_bb;
    push_loop(ctx, cond_bb, end_bb);
    
    if (fn->iter_type.base == TYPE_CLASS && fn->iter_type.ptr_depth == 0) {
        fn->iter_type.ptr_depth = 1;
    }
    
    AlirValue *val = new_temp(ctx, fn->iter_type); 
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_GET, val, iter, NULL));
    
    if (ctx->in_flux_resume) {
        AlirSymbol *sym = alir_find_symbol(ctx, fn->var_name);
        if (sym) { emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, sym->ptr)); }
    } else {
        AlirValue *var_ptr = new_temp(ctx, fn->iter_type); 
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, var_ptr, NULL, NULL));
        alir_add_symbol(ctx, fn->var_name, var_ptr, fn->iter_type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, var_ptr));
    }
    
    ASTNode *s = fn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
    
    if (!ctx->current_block->tail || !is_terminator(ctx->current_block->tail->op)) {
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_NEXT, NULL, iter, NULL));
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
    }
    
    pop_loop(ctx);
    ctx->current_block = end_bb;
}
