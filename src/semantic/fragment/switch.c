#include "semantic.h"

void sem_check_for_in(SemanticCtx *ctx, ASTNode *node) {
    ForInNode *fn = (ForInNode*)node;
    sem_check_expr(ctx, fn->collection);
    ctx->in_loop++;
    
    VarType col_type = sem_get_node_type(ctx, fn->collection);
    VarType iter_type = col_type;
    if (iter_type.array_size > 0) {
        iter_type.array_size = 0;
    } else if (iter_type.ptr_depth > 0) {
        iter_type.ptr_depth--;
    } else if (iter_type.vector_depth > 0) {
        iter_type.vector_depth--;
    } else if (iter_type.base == TYPE_STRING) {
        iter_type.base = TYPE_CHAR;
    } else {
        sem_error(ctx, node, "Cannot iterate over non-iterable type");
        iter_type = (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
    }
    
    fn->iter_type = iter_type; 
    
    sem_scope_enter(ctx, 0, (VarType){0});
    SemSymbol *s = sem_symbol_add(ctx, fn->var_name, SYM_VAR, iter_type);
    s->is_initialized = 1; 
    
    sem_check_block(ctx, fn->body);
    sem_scope_exit(ctx);
    
    ctx->in_loop--;
}

void sem_check_unary_op_switch(SemanticCtx *ctx, ASTNode *node) {
    UnaryOpNode *un = (UnaryOpNode*)node;
    sem_check_expr(ctx, un->operand);
    VarType t = sem_get_node_type(ctx, un->operand);
    
    if (sem_get_node_tainted(ctx, un->operand)) {
        sem_set_node_tainted(ctx, node, 1);
    }
    
    if (t.base == TYPE_VOID && t.ptr_depth == 0) {
         sem_error(ctx, node, "Operand of unary expression cannot be 'void'");
    }
    
    if (un->op == TOKEN_AND) { 
        t.ptr_depth++;
    } else if (un->op == TOKEN_STAR) { 
        if (t.ptr_depth > 0) t.ptr_depth--;
        else sem_error(ctx, node, "Cannot dereference non-pointer");
    } else if (un->op == TOKEN_NOT) {
        t = (VarType){TYPE_BOOL, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
    }
    sem_set_node_type(ctx, node, t);
}

void sem_check_var_ref(SemanticCtx *ctx, ASTNode *node) {
    VarRefNode *ref = (VarRefNode*)node;
    SemScope *found_in_scope = NULL;
    SemSymbol *sym = sem_symbol_lookup(ctx, ref->name, &found_in_scope);
    
    if (sym) {
        sem_set_node_type(ctx, node, sym->type);
        
        if (ctx->current_func_sym && ctx->current_func_sym->is_pure) {
            if (found_in_scope == ctx->global_scope) {
                sem_error(ctx, node, "Pure function '%s' cannot read global variable '%s'", ctx->current_func_sym->name, ref->name);
            }
        }

        if (!sym->is_pristine) {
            sem_set_node_tainted(ctx, node, 1);
        }

        if (sym->kind == SYM_VAR && !sym->is_initialized) {
            sem_error(ctx, node, "Use of uninitialized variable '%s'", ref->name);
        }

        if (found_in_scope && found_in_scope->is_class_scope) {
            ref->is_class_member = 1;
        } else {
            ref->is_class_member = 0;
        }

    } else {
        sem_error(ctx, node, "Undefined variable '%s'", ref->name);
        sem_set_node_type(ctx, node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    }
}

void sem_check_array_access(SemanticCtx *ctx, ASTNode *node) {
    ArrayAccessNode *aa = (ArrayAccessNode*)node;
    sem_check_expr(ctx, aa->target);
    sem_check_expr(ctx, aa->index);
    
    if (sem_get_node_tainted(ctx, aa->target)) {
        sem_set_node_tainted(ctx, node, 1);
    }
    
    VarType t = sem_get_node_type(ctx, aa->target);
    if (t.array_size > 0) t.array_size = 0;
    else if (t.ptr_depth > 0) t.ptr_depth--;
    else if (t.vector_depth > 0) t.vector_depth--;
    else if (t.base == TYPE_ENUM || t.base == TYPE_ARRAY || t.base == TYPE_STRING || t.base == TYPE_VECTOR 
|| t.base == TYPE_HASHMAP) {
         sem_set_node_type(ctx, node, (VarType){TYPE_STRING, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
         return;
    }
    else {
        sem_error(ctx, node, "Type is not a pointer, array, string, vector, hashmap, or enum");
        t = (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
    }
    sem_set_node_type(ctx, node, t);
}
