#include "semantic.h"

void sem_lookup_class_call(SemanticCtx *ctx, MethodCallNode *node) {
    VarType obj_type = sem_get_node_type(ctx, node->object);

    SemSymbol *class_sym = sem_symbol_lookup(ctx, obj_type.class_name, NULL);
    if (!class_sym || class_sym->kind != SYM_CLASS) {
        sem_error(ctx, (ASTNode*)node, "Type '%s' is not a class/struct", obj_type.class_name);
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        return;
    }
    
    SemSymbol *current_class = class_sym;
    int found = 0;
    
    while (current_class) {
        if (current_class->inner_scope) {
            SemSymbol *member = current_class->inner_scope->symbols;
            while (member) {
                if (strcmp(member->name, node->method_name) == 0) {
                    
                    if (ctx->current_func_sym && ctx->current_func_sym->is_pure) {
                        if (member->kind == SYM_FUNC && !member->is_pure) {
                            sem_error(ctx, (ASTNode*)node, "Pure function '%s' cannot call impure method '%s'", ctx->current_func_sym->name, member->name);
                        }
                    }

                    if (member->kind == SYM_FUNC && !member->is_pristine) {
                        sem_set_node_tainted(ctx, (ASTNode*)node, 1);
                    }

                    if (member->kind == SYM_FUNC) {
                        sem_set_node_type(ctx, (ASTNode*)node, member->type); 
                        node->owner_class = current_class->name; 
                        found = 1;
                    } 
                    else if (member->kind == SYM_VAR && member->type.is_func_ptr) {
                         sem_set_node_type(ctx, (ASTNode*)node, *member->type.fp_ret_type);
                         found = 1;
                    }

                    if (found) {
                        int arg_count = 0;
                        ASTNode **curr_arg = &node->args;
                        while(*curr_arg) {
                            sem_check_expr(ctx, *curr_arg);
                            
                            if (member->kind == SYM_FUNC && member->param_types && arg_count < member->param_count) {
                                sem_insert_implicit_cast(ctx, curr_arg, member->param_types[arg_count]);
                            }
                            
                            curr_arg = &(*curr_arg)->next;
                            arg_count++;
                        }
                        goto done_method_search;
                    }
                }
                member = member->next;
            }
        }
        if (current_class->parent_name) {
            current_class = sem_symbol_lookup(ctx, current_class->parent_name, NULL);
        } else {
            current_class = NULL;
        }
    }
    
    done_method_search:
    if (!found) {
         sem_error(ctx, (ASTNode*)node, "Method '%s' not found in class '%s'", node->method_name, obj_type.class_name);
         sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    }
}
