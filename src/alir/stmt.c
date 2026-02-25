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
            AlirSymbol *sym = alir_find_symbol(ctx, vn->name);
            if (sym && vn->initializer) {
                AlirValue *val = alir_gen_expr(ctx, vn->initializer);
                if (!val) val = alir_const_int(ctx->module, 0); // Safety net
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, sym->ptr));
            }
            return; 
        }
    }

    switch(node->type) {
        case NODE_CLEAN:
        case NODE_WASH:
            break;
        case NODE_VAR_DECL: {
            alir_stmt_vardecl(ctx, node);
            break;
        }
        case NODE_ASSIGN: {
            alir_stmt_assign(ctx, node);
            break;
        }
        case NODE_SWITCH: alir_gen_switch(ctx, (SwitchNode*)node); break;
        case NODE_EMIT: alir_gen_flux_yield(ctx, (EmitNode*)node); break;
        
        case NODE_WHILE: {
            alir_stmt_while(ctx, node);
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
            alir_stmt_for_in(ctx, node);
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
        case NODE_VECTOR_LIT:
        case NODE_VECTOR_ACCESS:
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
            
            if (!ctx->current_block->tail || !is_terminator(ctx->current_block->tail->op)) {
                emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, merge_bb->label), NULL));
            }
            
            if (in->else_body) {
                ctx->current_block = else_bb;
                s = in->else_body; while(s){ alir_gen_stmt(ctx,s); s=s->next; }
                if (!ctx->current_block->tail || !is_terminator(ctx->current_block->tail->op)) {
                    emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, merge_bb->label), NULL));
                }
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
