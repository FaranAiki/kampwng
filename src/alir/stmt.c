#include "alir.h"

void alir_gen_stmt(AlirCtx *ctx, ASTNode *node) {
    if (!node) return;
    
    ctx->current_line = node->line;
    ctx->current_col = node->col;

    if (node->type == NODE_VAR_DECL && ctx->in_flux_resume) {
        VarDeclNode *vn = (VarDeclNode*)node;
        FluxVar *fv = ctx->flux_vars;
        while(fv) {
            if (strcmp(fv->name, vn->name) == 0) break;
            fv = fv->next;
        }
        
        if (fv) {
            VarType ptr_type = vn->var_type;
            ptr_type.ptr_depth++;
            AlirValue *ptr = new_temp(ctx, ptr_type);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, fv->index)));
            
            alir_add_symbol(ctx, vn->name, ptr, vn->var_type);
            
            if (vn->initializer) {
                AlirValue *val = alir_gen_expr(ctx, vn->initializer);
                if (!val) val = alir_const_int(ctx->module, 0); // Safety net
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
            }
            return; 
        }
    }

    switch(node->type) {
        case NODE_CLEAN:
        case NODE_WASH:
            break;
        case NODE_VAR_DECL: {
            VarDeclNode *vn = (VarDeclNode*)node;
            AlirValue *ptr = new_temp(ctx, vn->var_type);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
            alir_add_symbol(ctx, vn->name, ptr, vn->var_type);
            if (vn->initializer) {
                AlirValue *val = alir_gen_expr(ctx, vn->initializer);
                if (!val) val = alir_const_int(ctx->module, 0); // Safety net
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
            }
            break;
        }
        case NODE_ASSIGN: {
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
                            ptr = new_temp(ctx, (VarType){TYPE_AUTO, 1, NULL});
                            emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, this_ptr, alir_const_int(ctx->module, idx)));
                        }
                    }
                    if (!ptr) ptr = alir_val_global(ctx->module, an->name, (VarType){TYPE_AUTO,0,NULL}); 
                }
            }
            
            if (!ptr) {
                ptr = new_temp(ctx, (VarType){TYPE_INT, 1, NULL});
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
            break;
        }
        case NODE_SWITCH: alir_gen_switch(ctx, (SwitchNode*)node); break;
        case NODE_EMIT: alir_gen_flux_yield(ctx, (EmitNode*)node); break;
        
        case NODE_WHILE: {
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
            break;
        }

        case NODE_LOOP: {
            LoopNode *ln = (LoopNode*)node;
            AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "loop_body");
            AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "loop_end");
            
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, body_bb->label), NULL));
            
            ctx->current_block = body_bb;
            push_loop(ctx, body_bb, end_bb);
            
            ASTNode *s = ln->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
            pop_loop(ctx);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, body_bb->label), NULL));
            
            ctx->current_block = end_bb;
            break;
        }

        case NODE_FOR_IN: {
            ForInNode *fn = (ForInNode*)node;
            AlirValue *col = alir_gen_expr(ctx, fn->collection);
            if (!col) {
                col = new_temp(ctx, (VarType){TYPE_AUTO, 1, NULL, 0, 0});
                emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, col, NULL, NULL));
            }
            
            AlirValue *iter = new_temp(ctx, (VarType){TYPE_VOID, 1, NULL, 0, 0}); 
            emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_INIT, iter, col, NULL));
            
            AlirBlock *cond_bb = alir_add_block(ctx->module, ctx->current_func, "for_cond");
            AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "for_body");
            AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "for_end");
            
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
            
            ctx->current_block = cond_bb;
            AlirValue *valid = new_temp(ctx, (VarType){TYPE_BOOL, 0, NULL, 0, 0});
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
                    AlirValue *ptr = new_temp(ctx, (VarType){TYPE_INT, 1, NULL, 0, 0}); 
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
            break;
        }

        case NODE_BREAK:
            if (ctx->loop_break) emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, ctx->loop_break->label), NULL));
            break;
            
        case NODE_CONTINUE:
            if (ctx->loop_continue) emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, ctx->loop_continue->label), NULL));
            break;

        case NODE_RETURN: {
            ReturnNode *rn = (ReturnNode*)node;
            if (ctx->in_flux_resume) {
                AlirValue *fin_ptr = new_temp(ctx, (VarType){TYPE_BOOL, 1});
                emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, fin_ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 1))); 
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 1), fin_ptr));
                emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
            } else {
                AlirValue *v = NULL;
                if (rn->value) {
                    v = alir_gen_expr(ctx, rn->value);
                    if (!v) v = alir_const_int(ctx->module, 0); // Safety net
                }
                emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, v, NULL));
            }
            break;
        }
        
        case NODE_CALL: 
        case NODE_METHOD_CALL:
        case NODE_VAR_REF:
        case NODE_BINARY_OP:
        case NODE_UNARY_OP:
        case NODE_LITERAL:
        case NODE_ARRAY_LIT:
        case NODE_ARRAY_ACCESS:
        case NODE_MEMBER_ACCESS:
        case NODE_TRAIT_ACCESS:
        case NODE_TYPEOF:
        case NODE_HAS_METHOD:
        case NODE_HAS_ATTRIBUTE:
        case NODE_CAST:
        case NODE_INC_DEC:
            alir_gen_expr(ctx, node); 
            break;

        case NODE_IF: {
            IfNode *in = (IfNode*)node;
            AlirValue *cond = alir_gen_expr(ctx, in->condition);
            if (!cond) cond = alir_const_int(ctx->module, 0); // Safety net

            AlirBlock *then_bb = alir_add_block(ctx->module, ctx->current_func, "then");
            AlirBlock *else_bb = alir_add_block(ctx->module, ctx->current_func, "else");
            AlirBlock *merge_bb = alir_add_block(ctx->module, ctx->current_func, "merge");
            
            AlirBlock *target_else = in->else_body ? else_bb : merge_bb;
            
            AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, cond, alir_val_label(ctx->module, then_bb->label));
            br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
            br->args[0] = alir_val_label(ctx->module, target_else->label);
            br->arg_count = 1;
            emit(ctx, br);
            
            ctx->current_block = then_bb;
            ASTNode *s = in->then_body; while(s){ alir_gen_stmt(ctx,s); s=s->next; }
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, merge_bb->label), NULL));
            
            if (in->else_body) {
                ctx->current_block = else_bb;
                s = in->else_body; while(s){ alir_gen_stmt(ctx,s); s=s->next; }
                emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, merge_bb->label), NULL));
            }
            
            ctx->current_block = merge_bb;
            break;
        }

        case NODE_ROOT:
        case NODE_FUNC_DEF:
        case NODE_CLASS:
        case NODE_NAMESPACE:
        case NODE_ENUM:
        case NODE_LINK:
        case NODE_CASE:
            break;
    }
}

