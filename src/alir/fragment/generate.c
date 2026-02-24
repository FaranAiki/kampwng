#include "alir.h"

void alir_stmt_vardecl(AlirCtx *ctx, ASTNode *node) {
    VarDeclNode *vn = (VarDeclNode*)node;
    AlirValue *ptr = new_temp(ctx, vn->var_type);
    
    // FIX: If it's an array, make sure the alloca allocates the array type
    // ensure ptr->type correctly reflects the array structure.
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
    alir_add_symbol(ctx, vn->name, ptr, vn->var_type);
    
    if (vn->initializer) {
        if (vn->initializer->type == NODE_ARRAY_LIT) {
            ArrayLitNode *al = (ArrayLitNode*)vn->initializer;
            ASTNode *elem = al->elements;
            int idx = 0;
            while(elem) {
                AlirValue *eval = alir_gen_expr(ctx, elem);
                VarType elem_t = vn->var_type;
                elem_t.array_size = 0;
                elem_t.ptr_depth++;
                
                AlirValue *elem_ptr = new_temp(ctx, elem_t);
                emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, elem_ptr, ptr, alir_const_int(ctx->module, idx)));
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, eval ? eval : alir_const_int(ctx->module, 0), elem_ptr));
                elem = elem->next; idx++;
            }
        } else {
            AlirValue *val = alir_gen_expr(ctx, vn->initializer);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val ? val : alir_const_int(ctx->module, 0), ptr));
        }
    }
}

void alir_stmt_assign(AlirCtx *ctx, ASTNode *node) {
    AssignNode *an = (AssignNode*)node;
    AlirValue *ptr = NULL;
    
    if (an->target) {
        ptr = alir_gen_addr(ctx, an->target);
        // ... fallback logic remains ...
    } else if (an->name) {
        AlirSymbol *s = alir_find_symbol(ctx, an->name);
        if (s) { 
            ptr = s->ptr;
        } else {
            // [FIX] Struct Field as Global Bug: Handle implicit `this.field`
            AlirSymbol *this_sym = alir_find_symbol(ctx, "this");
            if (this_sym && this_sym->type.class_name) {
                int idx = alir_get_field_index(ctx->module, this_sym->type.class_name, an->name);
                if (idx != -1) {
                    AlirValue *this_ptr = new_temp(ctx, this_sym->type);
                    emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, this_ptr, this_sym->ptr, NULL));
                    ptr = new_temp(ctx, (VarType){TYPE_AUTO, 1, 0, NULL});
                    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, this_ptr, alir_const_int(ctx->module, idx)));
                }
            }
            if (!ptr) ptr = alir_val_global(ctx->module, an->name, (VarType){TYPE_AUTO, 0, 0, NULL}); 
        }
    }
    
    if (!ptr) {
        ptr = new_temp(ctx, (VarType){TYPE_INT, 1, 0, NULL});
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
    }
    
    AlirValue *val = alir_gen_expr(ctx, an->value);
    if (!val) val = alir_const_int(ctx->module, 0);
    
    // [FIX] Reconstruct lost Semantic Analyzer typing dynamically
    if (an->name) {
        AlirSymbol *sym = alir_find_symbol(ctx, an->name);
        if (sym && !sym->type.class_name && val->type.class_name) {
            sym->type.class_name = alir_strdup(ctx->module, val->type.class_name);
            sym->type.base = val->type.base;
        }
    }
    
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
}

void alir_stmt_while(AlirCtx *ctx, ASTNode *node) {
    WhileNode *wn = (WhileNode*)node;
    AlirBlock *cond_bb = alir_add_block(ctx->module, ctx->current_func, "while_cond");
    AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "while_body");
    AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "while_end");


    if (wn->is_do_while) {
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, body_bb->label), NULL));
        
        ctx->current_block = body_bb;
        push_loop(ctx, cond_bb, end_bb);
        ASTNode *s = wn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
        pop_loop(ctx);
        
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));

        ctx->current_block = cond_bb;
        AlirValue *cond = alir_gen_expr(ctx, wn->condition);
        if (!cond) cond = alir_const_int(ctx->module, 0); // Safety net

        AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, cond, alir_val_label(ctx->module, body_bb->label));
        br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
        br->args[0] = alir_val_label(ctx->module, end_bb->label);
        br->arg_count = 1;
        emit(ctx, br);
    } else {
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));

        ctx->current_block = cond_bb;
        AlirValue *cond = alir_gen_expr(ctx, wn->condition);
        if (!cond) cond = alir_const_int(ctx->module, 0); // Safety net

        AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, cond, alir_val_label(ctx->module, body_bb->label));
        br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
        br->args[0] = alir_val_label(ctx->module, end_bb->label);
        br->arg_count = 1;
        emit(ctx, br);

        ctx->current_block = body_bb;
        push_loop(ctx, cond_bb, end_bb);
        ASTNode *s = wn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
        pop_loop(ctx);
        
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
    }
    ctx->current_block = end_bb;
}

void alir_stmt_for_in(AlirCtx *ctx, ASTNode *node) {
    ForInNode *fn = (ForInNode*)node;
    AlirValue *col = alir_gen_expr(ctx, fn->collection);
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
    
    AlirValue *val = new_temp(ctx, fn->iter_type); 
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_GET, val, iter, NULL));
    
    if (ctx->in_flux_resume) {
        FluxVar *fv = ctx->flux_vars;
        while(fv) { if(strcmp(fv->name, fn->var_name)==0) break; fv=fv->next; }
        if (fv) {
            AlirValue *ptr = new_temp(ctx, (VarType){TYPE_INT, 1, 0, NULL, 0, 0}); 
            emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, fv->index)));
            alir_add_symbol(ctx, fn->var_name, ptr, fn->iter_type);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
        }
    } else {
        AlirValue *var_ptr = new_temp(ctx, fn->iter_type); 
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, var_ptr, NULL, NULL));
        alir_add_symbol(ctx, fn->var_name, var_ptr, fn->iter_type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, var_ptr));
    }
    
    ASTNode *s = fn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
    
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_NEXT, NULL, iter, NULL));
    emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
    
    pop_loop(ctx);
    ctx->current_block = end_bb;
}
