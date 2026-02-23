#include "semantic.h"
#include <stdio.h>

void sem_check_implicit_cast(SemanticCtx *ctx, ASTNode *node, VarType dest, VarType src) {
    int dest_is_str = (dest.base == TYPE_STRING && dest.ptr_depth == 0);
    int src_is_char = (src.base == TYPE_CHAR && (src.ptr_depth > 0 || src.array_size > 0));
    
    int dest_is_char = (dest.base == TYPE_CHAR && (dest.ptr_depth > 0 || dest.array_size > 0));
    int src_is_str = (src.base == TYPE_STRING && src.ptr_depth == 0);
    
    if (dest_is_str && src_is_char) {
        sem_info(ctx, node, "Implicit cast from 'char%s' to 'string'", (src.array_size > 0) ? "[]" : "*");
    } else if (dest_is_char && src_is_str) {
        sem_info(ctx, node, "Implicit cast from 'string' to 'char%s'", (dest.array_size > 0) ? "[]" : "*");
        
        if (node->type == NODE_LITERAL) {
            LiteralNode *lit = (LiteralNode*)node;
            if (lit->var_type.base == TYPE_STRING && lit->val.str_val) {
                sem_hint(ctx, node, "Use c\"%s\" for a C-style string", lit->val.str_val);
                return;
            }
        }
        sem_hint(ctx, node, "Use c\"...\" for a C-style string");
    }
}

void sem_check_var_decl(SemanticCtx *ctx, VarDeclNode *node, int register_sym) {
    if (node->is_array && node->var_type.ptr_depth == 0) {
        ASTNode *dim = node->array_size;
        while(dim) {
            node->var_type.ptr_depth++;
            dim = dim->next;
        }
        if (node->array_size && node->array_size->type == NODE_LITERAL) {
             node->var_type.array_size = (int)((LiteralNode*)node->array_size)->val.long_val;
        }
    }

    if (node->initializer) {
        sem_check_expr(ctx, node->initializer);
        VarType init_type = sem_get_node_type(ctx, node->initializer);
        
        if (init_type.base == TYPE_VOID && init_type.ptr_depth == 0) {
            sem_error(ctx, (ASTNode*)node, "Cannot use expression of type 'void' to initialize variable '%s'", node->name);
        }

        int expr_tainted = sem_get_node_tainted(ctx, node->initializer);
        if (node->is_pristine && expr_tainted) {
            node->is_pristine = false;
        }

        if (node->var_type.base == TYPE_AUTO) {
            if (init_type.base == TYPE_UNKNOWN) {
                sem_error(ctx, (ASTNode*)node, "Cannot infer type for variable '%s' (unknown initializer type)", node->name);
            } else if (init_type.base == TYPE_VOID && init_type.ptr_depth == 0) {
                sem_error(ctx, (ASTNode*)node, "Cannot infer type 'void' for variable '%s'", node->name);
            } else {
                node->var_type = init_type; 
            }
        } 
        else {
            if (!sem_types_are_compatible(node->var_type, init_type)) {
                char *t1 = sem_type_to_str(node->var_type);
                char *t2 = sem_type_to_str(init_type);
                sem_error(ctx, (ASTNode*)node, "Type mismatch in declaration of '%s'. Expected '%s', got '%s'", node->name, t1, t2);
            } else {
                sem_check_implicit_cast(ctx, (ASTNode*)node, node->var_type, init_type);
            }
        }
    } else {
        if (node->var_type.base == TYPE_AUTO) {
            sem_error(ctx, (ASTNode*)node, "Variable '%s' declared 'let' but has no initializer", node->name);
        }
    }

    if (register_sym) {
        if (lookup_local_symbol(ctx, node->name)) {
            sem_error(ctx, (ASTNode*)node, "Redeclaration of variable '%s' in the same scope", node->name);
        } else {
            SemScope *shadow_scope = NULL;
            SemSymbol *shadow = sem_symbol_lookup(ctx, node->name, &shadow_scope);
            if (shadow) {
                if (shadow->inner_scope == ctx->global_scope) {
                    sem_info(ctx, (ASTNode*)node, "Shadowing global variable '%s'", node->name);
                } 
                else if (shadow_scope && shadow_scope->is_class_scope) {
                    sem_info(ctx, (ASTNode*)node, "Shadowing class member '%s'", node->name);
                }
                else {
                    sem_info(ctx, (ASTNode*)node, "Shadowing variable '%s' from outer scope", node->name);
                }
            }

            SemSymbol *sym = sem_symbol_add(ctx, node->name, SYM_VAR, node->var_type);
            sym->is_mutable = node->is_mutable; 
            sym->is_pure = node->is_pure;
            sym->is_pristine = node->is_pristine;
            
            int is_global = (ctx->current_scope == ctx->global_scope);
            if (node->initializer || is_global || node->base.type == NODE_VAR_DECL) {
                 sym->is_initialized = 1;
            } else {
                 sym->is_initialized = 0;
            }
        }
    } else {
        SemSymbol *sym = lookup_local_symbol(ctx, node->name);
        if (sym) {
            sym->type = node->var_type;
            sym->is_mutable = node->is_mutable;
            sym->is_pure = node->is_pure;
            sym->is_pristine = node->is_pristine;
            if (node->initializer) sym->is_initialized = 1;
        }
    }

    if (ctx->current_scope && ctx->current_scope->is_class_scope && node->var_type.base == TYPE_CLASS && node->var_type.class_name) {
        SemSymbol *type_sym = sem_symbol_lookup(ctx, node->var_type.class_name, NULL);
        if (type_sym && type_sym->kind == SYM_CLASS) {
            if (type_sym->is_has_a == HAS_A_INERT) {
                sem_error(ctx, (ASTNode*)node, "Class '%s' is inert, thus cannot be implicitly composed as field '%s'", type_sym->name, node->name);
            }
            type_sym->is_used_as_composition = 1;
        }
    }
}

void sem_check_assign(SemanticCtx *ctx, AssignNode *node) {
    sem_check_expr(ctx, node->value);
    VarType rhs_type = sem_get_node_type(ctx, node->value);
    VarType lhs_type;
    int expr_tainted = sem_get_node_tainted(ctx, node->value);
    
    if (rhs_type.base == TYPE_VOID && rhs_type.ptr_depth == 0) {
        sem_error(ctx, (ASTNode*)node, "Cannot assign value of type 'void' to variable");
    }

    if (ctx->current_func_sym && ctx->current_func_sym->is_pure && node->name) {
        SemScope *scope = NULL;
        SemSymbol *sym = sem_symbol_lookup(ctx, node->name, &scope);
        if (sym && scope == ctx->global_scope) {
            sem_error(ctx, (ASTNode*)node, "Pure function '%s' cannot modify global variable '%s'", ctx->current_func_sym->name, sym->name);
        }
    }

    if (node->name) {
        SemSymbol *sym = sem_symbol_lookup(ctx, node->name, NULL);
        if (!sym) {
            sem_error(ctx, (ASTNode*)node, "Undefined variable '%s'", node->name);
            lhs_type = (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
        } else {
            if (!sym->is_mutable) {
                sem_error(ctx, (ASTNode*)node, "Cannot assign to immutable variable '%s'", node->name);
            }
            
            if (sym->is_pristine && expr_tainted) {
                sem_error(ctx, (ASTNode*)node, "Cannot assign a tainted value to pristine variable '%s'", sym->name);
            }
            
            if (node->op != TOKEN_ASSIGN) {
                if (sym->kind == SYM_VAR && !sym->is_initialized) {
                    sem_error(ctx, (ASTNode*)node, "Use of uninitialized variable '%s' in compound assignment", node->name);
                }
            }

            lhs_type = sym->type;
            
            if (node->index) {
                sem_check_expr(ctx, node->index);
                VarType idx_t = sem_get_node_type(ctx, node->index);
                if (!is_integer(idx_t)) {
                    sem_error(ctx, node->index, "Array index must be an integer");
                }
                
                if (lhs_type.array_size > 0) lhs_type.array_size = 0;
                else if (lhs_type.ptr_depth > 0) lhs_type.ptr_depth--;
                else if (lhs_type.vector_depth > 0) lhs_type.vector_depth--;
                else {
                    sem_error(ctx, (ASTNode*)node, "Cannot index into non-array variable '%s'", node->name);
                }
                
                if (sym->kind == SYM_VAR && !sym->is_initialized) {
                    sem_error(ctx, (ASTNode*)node, "Use of uninitialized array '%s'", node->name);
                }
            } else {
                if (node->op == TOKEN_ASSIGN) {
                    sym->is_initialized = 1;
                }
            }
        }
    } else {
        sem_check_expr(ctx, node->target);
        lhs_type = sem_get_node_type(ctx, node->target);
    }

    if (lhs_type.base != TYPE_UNKNOWN && rhs_type.base != TYPE_UNKNOWN) {
        if (!sem_types_are_compatible(lhs_type, rhs_type)) {
             char *t1 = sem_type_to_str(lhs_type);
             char *t2 = sem_type_to_str(rhs_type);
             sem_error(ctx, (ASTNode*)node, "Invalid assignment. Cannot assign '%s' to '%s'", t2, t1);
        } else {
             sem_check_implicit_cast(ctx, (ASTNode*)node, lhs_type, rhs_type);
        }
    }
}

int is_numeric(VarType t) {
    return ((t.base >= TYPE_INT && t.base <= TYPE_LONG_DOUBLE) || t.base == TYPE_ENUM) && t.ptr_depth == 0 && t.vector_depth == 0;
}

int is_integer(VarType t) {
    return ((t.base >= TYPE_INT && t.base <= TYPE_CHAR) || t.base == TYPE_ENUM) && t.ptr_depth == 0 && t.vector_depth == 0;
}

int is_bool(VarType t) {
    return (t.base == TYPE_BOOL && t.ptr_depth == 0 && t.vector_depth == 0);
}

int is_pointer(VarType t) {
    return t.ptr_depth > 0 || t.array_size > 0 || t.base == TYPE_STRING || t.is_func_ptr;
}
