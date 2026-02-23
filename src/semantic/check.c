#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

SemSymbol* lookup_local_symbol(SemanticCtx *ctx, const char *name) {
    if (!ctx->current_scope) return NULL;
    SemSymbol *sym = ctx->current_scope->symbols;
    while (sym) {
        if (strcmp(sym->name, name) == 0) return sym;
        sym = sym->next;
    }
    return NULL;
}

void sem_insert_implicit_cast(SemanticCtx *ctx, ASTNode **node_ptr, VarType target_type) {
    if (!node_ptr || !*node_ptr) return;
    VarType current = sem_get_node_type(ctx, *node_ptr);
    
    if (current.base == target_type.base && current.ptr_depth == target_type.ptr_depth && current.vector_depth == target_type.vector_depth) return;
    if (current.base == TYPE_UNKNOWN || target_type.base == TYPE_UNKNOWN) return;
    if (target_type.base == TYPE_VOID) return;

    CastNode *cast = arena_alloc_type(ctx->compiler_ctx->arena, CastNode);
    cast->base.type = NODE_CAST;
    cast->base.line = (*node_ptr)->line;
    cast->base.col = (*node_ptr)->col;
    
    cast->base.next = (*node_ptr)->next; 
    (*node_ptr)->next = NULL; 
    
    cast->var_type = target_type;
    cast->operand = *node_ptr;
    
    sem_set_node_type(ctx, (ASTNode*)cast, target_type);
    
    *node_ptr = (ASTNode*)cast;
}

void sem_register_builtins(SemanticCtx *ctx) {
    VarType int_t = {TYPE_INT, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
    sem_symbol_add(ctx, "printf", SYM_FUNC, int_t);
    sem_symbol_add(ctx, "print", SYM_FUNC, int_t);
    
    VarType void_ptr = {TYPE_VOID, 1, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
    sem_symbol_add(ctx, "malloc", SYM_FUNC, void_ptr);
    sem_symbol_add(ctx, "alloc", SYM_FUNC, void_ptr);
    
    VarType void_t = {TYPE_VOID, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
    sem_symbol_add(ctx, "free", SYM_FUNC, void_t);

    VarType str_t = {TYPE_STRING, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
    sem_symbol_add(ctx, "input", SYM_FUNC, str_t);
    
    sem_symbol_add(ctx, "exit", SYM_FUNC, void_t);
}

void sem_scan_class_members(SemanticCtx *ctx, ClassNode *cn, SemSymbol *class_sym) {
    if (!ctx->compiler_ctx || !ctx->compiler_ctx->arena) return;

    SemScope *class_scope = arena_alloc_type(ctx->compiler_ctx->arena, SemScope);
    memset(class_scope, 0, sizeof(SemScope));

    class_scope->symbols = NULL;
    class_scope->parent = ctx->current_scope; 
    class_scope->is_function_scope = 0;
    class_scope->is_class_scope = 1; 
    class_scope->class_sym = class_sym; 
    class_scope->expected_ret_type = (VarType){0};
    
    class_sym->inner_scope = class_scope;
    
    SemScope *old_scope = ctx->current_scope;
    ctx->current_scope = class_scope;
    
    ASTNode *mem = cn->members;
    while(mem) {
        if (mem->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)mem;
            SemSymbol *sym = sem_symbol_add(ctx, vd->name, SYM_VAR, vd->var_type);
            sym->is_mutable = vd->is_mutable;
            sym->is_pure = vd->is_pure;
            sym->is_pristine = vd->is_pristine;
        } else if (mem->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)mem;
            SemSymbol *sym = sem_symbol_add(ctx, fd->name, SYM_FUNC, fd->ret_type);
            sym->is_is_a = fd->is_is_a;
            sym->is_has_a = fd->is_has_a;
            sym->is_pure = fd->is_pure;
            sym->is_pristine = fd->is_pristine;
        }
        mem = mem->next;
    }
    
    ctx->current_scope = old_scope;
}

void sem_scan_top_level(SemanticCtx *ctx, ASTNode *node) {
    if (!ctx->compiler_ctx || !ctx->compiler_ctx->arena) return;

    while (node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            SemSymbol *sym = sem_symbol_add(ctx, fd->name, SYM_FUNC, fd->ret_type);
            sym->is_is_a = fd->is_is_a;
            sym->is_has_a = fd->is_has_a;
            sym->is_pure = fd->is_pure;
            sym->is_pristine = fd->is_pristine;
        }
        else if (node->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)node;
            SemSymbol *sym = sem_symbol_add(ctx, vd->name, SYM_VAR, vd->var_type);
            sym->is_mutable = vd->is_mutable;
            sym->is_pure = vd->is_pure;
            sym->is_pristine = vd->is_pristine;
        }
        else if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            VarType type_class = {TYPE_CLASS, 0, 0, arena_strdup(ctx->compiler_ctx->arena, cn->name), 0, NULL, NULL, 0, 0, 0, 0};
            SemSymbol *sym = sem_symbol_add(ctx, cn->name, SYM_CLASS, type_class);
            sym->is_is_a = cn->is_is_a;
            sym->is_has_a = cn->is_has_a;
            if (cn->parent_name) {
                sym->parent_name = arena_strdup(ctx->compiler_ctx->arena, cn->parent_name);
            }
            sem_scan_class_members(ctx, cn, sym);
        }
        else if (node->type == NODE_ENUM) {
            EnumNode *en = (EnumNode*)node;
            
            VarType enum_type = {TYPE_ENUM, 0, 0, arena_strdup(ctx->compiler_ctx->arena, en->name), 0, NULL, NULL, 0, 0, 0, 0};
            SemSymbol *sym = sem_symbol_add(ctx, en->name, SYM_ENUM, enum_type);
            
            SemScope *enum_scope = arena_alloc_type(ctx->compiler_ctx->arena, SemScope);
            memset(enum_scope, 0, sizeof(SemScope));

            enum_scope->symbols = NULL;
            enum_scope->parent = ctx->current_scope;
            enum_scope->is_function_scope = 0;
            enum_scope->is_class_scope = 0; 
            sym->inner_scope = enum_scope;
            
            EnumEntry *entry = en->entries;
            while(entry) {
                SemSymbol *mem = arena_alloc_type(ctx->compiler_ctx->arena, SemSymbol);
                memset(mem, 0, sizeof(SemSymbol));

                mem->name = arena_strdup(ctx->compiler_ctx->arena, entry->name);
                mem->kind = SYM_VAR; 
                mem->type = enum_type; 
                mem->is_mutable = 0;
                mem->is_initialized = 1;
                mem->is_pure = 1;
                mem->is_pristine = 1;
                mem->param_types = NULL;
                mem->param_count = 0;
                mem->parent_name = NULL;
                mem->inner_scope = NULL;
                
                mem->next = enum_scope->symbols;
                enum_scope->symbols = mem;
                
                entry = entry->next;
            }
        }
        else if (node->type == NODE_NAMESPACE) {
            NamespaceNode *ns = (NamespaceNode*)node;
            SemSymbol *sym = sem_symbol_add(ctx, ns->name, SYM_NAMESPACE, (VarType){TYPE_VOID, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
            
            SemScope *ns_scope = arena_alloc_type(ctx->compiler_ctx->arena, SemScope);
            memset(ns_scope, 0, sizeof(SemScope));

            ns_scope->symbols = NULL;
            ns_scope->parent = ctx->current_scope;
            ns_scope->is_function_scope = 0;
            ns_scope->is_class_scope = 0;
            sym->inner_scope = ns_scope;
            
            SemScope *old = ctx->current_scope;
            ctx->current_scope = ns_scope;
            sem_scan_top_level(ctx, ns->body);
            ctx->current_scope = old;
        }
        node = node->next;
    }
}

void sem_check_call(SemanticCtx *ctx, CallNode *node) {
    if (!ctx->compiler_ctx || !ctx->compiler_ctx->arena) return;

    SemSymbol *sym = sem_symbol_lookup(ctx, node->name, NULL);
    
    if (!sym) {
        sem_error(ctx, (ASTNode*)node, "Undefined function or class '%s'", node->name);
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        return;
    }
    
    if (ctx->current_func_sym && ctx->current_func_sym->is_pure) {
        if (sym->kind == SYM_FUNC && !sym->is_pure) {
            sem_error(ctx, (ASTNode*)node, "Pure function '%s' cannot call impure function '%s'", ctx->current_func_sym->name, sym->name);
        }
    }

    if (sym->kind == SYM_FUNC && !sym->is_pristine) {
        sem_set_node_tainted(ctx, (ASTNode*)node, 1);
    }
    
    int arg_count = 0;
    ASTNode **curr_arg = &node->args;
    while(*curr_arg) {
        sem_check_expr(ctx, *curr_arg);
        
        if (sym->kind == SYM_FUNC && sym->param_types && arg_count < sym->param_count) {
            sem_insert_implicit_cast(ctx, curr_arg, sym->param_types[arg_count]);
        }
        
        curr_arg = &(*curr_arg)->next;
        arg_count++;
    }

    if (sym->kind == SYM_CLASS) {
        VarType instance = {TYPE_CLASS, 1, 0, arena_strdup(ctx->compiler_ctx->arena, sym->name), 0, NULL, NULL, 0, 0, 0, 0}; 
        sem_set_node_type(ctx, (ASTNode*)node, instance);
    } else {
        sem_set_node_type(ctx, (ASTNode*)node, sym->type);
    }
}

void sem_check_binary_op(SemanticCtx *ctx, BinaryOpNode *node) {
    sem_check_expr(ctx, node->left);
    sem_check_expr(ctx, node->right);
    
    VarType l = sem_get_node_type(ctx, node->left);
    VarType r = sem_get_node_type(ctx, node->right);
    
    if (sem_get_node_tainted(ctx, node->left) || sem_get_node_tainted(ctx, node->right)) {
        sem_set_node_tainted(ctx, (ASTNode*)node, 1);
    }

    if (l.base == TYPE_UNKNOWN || r.base == TYPE_UNKNOWN) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        return;
    }

    if ((l.base == TYPE_VOID && l.ptr_depth == 0) || (r.base == TYPE_VOID && r.ptr_depth == 0)) {
        sem_error(ctx, (ASTNode*)node, "Operand of binary expression cannot be 'void'");
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        return;
    }
    
    if (node->op == TOKEN_AND_AND || node->op == TOKEN_OR_OR) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_BOOL, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        return;
    }
    
    if (node->op == TOKEN_EQ || node->op == TOKEN_NEQ || 
        node->op == TOKEN_LT || node->op == TOKEN_GT || 
        node->op == TOKEN_LTE || node->op == TOKEN_GTE) {
        
        if (is_numeric(l) && is_numeric(r)) {
            VarType target_type = {TYPE_INT, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
            if (l.base == TYPE_LONG_DOUBLE || r.base == TYPE_LONG_DOUBLE) target_type.base = TYPE_LONG_DOUBLE;
            else if (l.base == TYPE_DOUBLE || r.base == TYPE_DOUBLE) target_type.base = TYPE_DOUBLE;
            else if (l.base == TYPE_FLOAT || r.base == TYPE_FLOAT) target_type.base = TYPE_FLOAT;
            else if (l.base == TYPE_LONG || r.base == TYPE_LONG) target_type.base = TYPE_LONG;
            
            sem_insert_implicit_cast(ctx, &node->left, target_type);
            sem_insert_implicit_cast(ctx, &node->right, target_type);
        }
        
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_BOOL, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        return;
    }
    
    if (is_numeric(l) && is_numeric(r)) {
        VarType target_type = {TYPE_INT, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
        
        if (l.base == TYPE_LONG_DOUBLE || r.base == TYPE_LONG_DOUBLE) target_type.base = TYPE_LONG_DOUBLE;
        else if (l.base == TYPE_DOUBLE || r.base == TYPE_DOUBLE) target_type.base = TYPE_DOUBLE;
        else if (l.base == TYPE_FLOAT || r.base == TYPE_FLOAT) target_type.base = TYPE_FLOAT;
        else if (l.base == TYPE_LONG || r.base == TYPE_LONG) target_type.base = TYPE_LONG;
        
        sem_insert_implicit_cast(ctx, &node->left, target_type);
        sem_insert_implicit_cast(ctx, &node->right, target_type);
        
        sem_set_node_type(ctx, (ASTNode*)node, target_type);
    } 
    else if (is_pointer(l) && is_integer(r)) {
         sem_set_node_type(ctx, (ASTNode*)node, l);
    }
    else if (is_integer(l) && is_pointer(r)) {
         sem_set_node_type(ctx, (ASTNode*)node, r);
    }
    else if (l.base == TYPE_STRING || r.base == TYPE_STRING) {
         if (node->op == TOKEN_PLUS) 
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_STRING, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
         else {
            sem_error(ctx, (ASTNode*)node, "Invalid operation on strings");
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
         }
    }
    else {
        sem_error(ctx, (ASTNode*)node, "Invalid operands for binary operator");
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    }
}

void sem_check_member_access(SemanticCtx *ctx, MemberAccessNode *node) {
    sem_check_expr(ctx, node->object);
    VarType obj_type = sem_get_node_type(ctx, node->object);
    
    if (sem_get_node_tainted(ctx, node->object)) {
        sem_set_node_tainted(ctx, (ASTNode*)node, 1);
    }
    
    if (obj_type.base == TYPE_UNKNOWN) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        return;
    }
    
    if (obj_type.base == TYPE_CLASS && obj_type.class_name) {
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
                    if (strcmp(member->name, node->member_name) == 0) {
                        sem_set_node_type(ctx, (ASTNode*)node, member->type);
                        found = 1;
                        goto done_search;
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
        
        done_search:
        if (!found) {
            sem_error(ctx, (ASTNode*)node, "Class '%s' has no member named '%s'", obj_type.class_name, node->member_name);
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        }
    }
    else if (obj_type.base == TYPE_ENUM && obj_type.class_name) {
        SemSymbol *enum_sym = sem_symbol_lookup(ctx, obj_type.class_name, NULL);
        
        if (!enum_sym || enum_sym->kind != SYM_ENUM) {
            sem_error(ctx, (ASTNode*)node, "'%s' is not an enum", obj_type.class_name);
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
            return;
        }

        if (enum_sym->inner_scope) {
             SemSymbol *member = enum_sym->inner_scope->symbols;
             while (member) {
                 if (strcmp(member->name, node->member_name) == 0) {
                     sem_set_node_type(ctx, (ASTNode*)node, member->type);
                     return;
                 }
                 member = member->next;
             }
        }
        sem_error(ctx, (ASTNode*)node, "Enum '%s' has no member '%s'", obj_type.class_name, node->member_name);
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    }
    else if (obj_type.base == TYPE_STRING && strcmp(node->member_name, "length") == 0) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_INT, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    }
    else {
        sem_error(ctx, (ASTNode*)node, "Cannot access member on non-class/non-enum type");
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    }
}

void sem_check_method_call(SemanticCtx *ctx, MethodCallNode *node) {
    sem_check_expr(ctx, node->object);
    VarType obj_type = sem_get_node_type(ctx, node->object);
    
    if (sem_get_node_tainted(ctx, node->object)) {
        sem_set_node_tainted(ctx, (ASTNode*)node, 1);
    }
    
    if (obj_type.base == TYPE_UNKNOWN) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
        return;
    }

    if (obj_type.base == TYPE_CLASS && obj_type.class_name) {
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
    } else {
        sem_error(ctx, (ASTNode*)node, "Cannot call method on non-class type");
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    }
}

void sem_check_expr(SemanticCtx *ctx, ASTNode *node) {
    if (!node) return;
    
    switch(node->type) {
        case NODE_LITERAL: {
            LiteralNode *lit = (LiteralNode*)node;
            sem_set_node_type(ctx, node, lit->var_type);
            break;
        }
        case NODE_VAR_REF: {
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
            break;
        }
        case NODE_BINARY_OP: sem_check_binary_op(ctx, (BinaryOpNode*)node); break;
        case NODE_UNARY_OP: {
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
            break;
        }
        case NODE_CALL: sem_check_call(ctx, (CallNode*)node); break;
        case NODE_MEMBER_ACCESS: sem_check_member_access(ctx, (MemberAccessNode*)node); break;
        case NODE_ARRAY_ACCESS: {
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
            break;
        }
        case NODE_CAST: {
            CastNode *cn = (CastNode*)node;
            sem_check_expr(ctx, cn->operand);
            
            if (sem_get_node_tainted(ctx, cn->operand)) {
                sem_set_node_tainted(ctx, node, 1);
            }
            
            VarType op_t = sem_get_node_type(ctx, cn->operand);
            if (op_t.base == TYPE_VOID && op_t.ptr_depth == 0) {
                sem_error(ctx, node, "Cannot cast 'void' value");
            }

            sem_set_node_type(ctx, node, cn->var_type);
            break;
        }
        case NODE_METHOD_CALL: {
            sem_check_method_call(ctx, (MethodCallNode*)node);
            break;
        }
        case NODE_ARRAY_LIT: {
            ArrayLitNode *al = (ArrayLitNode*)node;
            ASTNode *el = al->elements;
            VarType elem_type = {TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
            if (el) {
                sem_check_expr(ctx, el);
                elem_type = sem_get_node_type(ctx, el);
                el = el->next;
            }
            while(el) {
                sem_check_expr(ctx, el);
                el = el->next;
            }
            elem_type.ptr_depth++;
            sem_set_node_type(ctx, node, elem_type);
            break;
        }
        case NODE_INC_DEC: {
            IncDecNode *id = (IncDecNode*)node;
            sem_check_expr(ctx, id->target);
            VarType t = sem_get_node_type(ctx, id->target);
            if (!is_numeric(t) && !is_pointer(t) && t.base != TYPE_UNKNOWN) {
                sem_error(ctx, node, "Cannot increment/decrement non-numeric/non-pointer type");
            }
            if (sem_get_node_tainted(ctx, id->target)) sem_set_node_tainted(ctx, node, 1);
            sem_set_node_type(ctx, node, t);
            break;
        }
        case NODE_ASSIGN: {
            AssignNode *an = (AssignNode*)node;
            sem_check_assign(ctx, an); 
            VarType t;
            if (an->name) {
                SemSymbol *sym = sem_symbol_lookup(ctx, an->name, NULL);
                t = sym ? sym->type : (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
            } else {
                t = sem_get_node_type(ctx, an->target);
            }
            sem_set_node_type(ctx, node, t);
            break;
        }
        case NODE_TRAIT_ACCESS: {
            TraitAccessNode *ta = (TraitAccessNode*)node;
            sem_check_expr(ctx, ta->object);
            if (sem_get_node_tainted(ctx, ta->object)) sem_set_node_tainted(ctx, node, 1);
            VarType res = {TYPE_CLASS, 1, 0, arena_strdup(ctx->compiler_ctx->arena, ta->trait_name), 0, NULL, NULL, 0, 0, 0, 0};
            sem_set_node_type(ctx, node, res);
            break;
        }
        case NODE_TYPEOF:
        case NODE_HAS_METHOD:
        case NODE_HAS_ATTRIBUTE: {
            UnaryOpNode *un = (UnaryOpNode*)node;
            sem_check_expr(ctx, un->operand);
            if (sem_get_node_tainted(ctx, un->operand)) sem_set_node_tainted(ctx, node, 1);
            
            if (node->type == NODE_TYPEOF) {
                sem_set_node_type(ctx, node, (VarType){TYPE_STRING, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
            } else {
                sem_set_node_type(ctx, node, (VarType){TYPE_BOOL, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
            }
            break;
        }
        default: break;
    }
}

void sem_check_stmt(SemanticCtx *ctx, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_VAR_DECL: sem_check_var_decl(ctx, (VarDeclNode*)node, 1); break;
        case NODE_ASSIGN: sem_check_assign(ctx, (AssignNode*)node); break;
        case NODE_RETURN: {
            ReturnNode *rn = (ReturnNode*)node;
            if (rn->value) {
                sem_check_expr(ctx, rn->value);
                VarType val = sem_get_node_type(ctx, rn->value);
                
                if (ctx->current_func_sym && ctx->current_func_sym->is_pristine && sem_get_node_tainted(ctx, rn->value)) {
                    sem_error(ctx, node, "Pristine function cannot return a tainted value");
                }

                if (ctx->current_scope->is_function_scope) {
                    if (!sem_types_are_compatible(ctx->current_scope->expected_ret_type, val)) {
                        sem_error(ctx, node, "Return type mismatch");
                    } else {
                         sem_insert_implicit_cast(ctx, &rn->value, ctx->current_scope->expected_ret_type);
                    }
                }
            } else {
                 if (ctx->current_scope->is_function_scope && ctx->current_scope->expected_ret_type.base != TYPE_VOID) {
                     sem_error(ctx, node, "Function must return a value");
                 }
            }
            break;
        }
        case NODE_WASH: {
            WashNode *wn = (WashNode*)node;
            SemSymbol *target_sym = sem_symbol_lookup(ctx, wn->var_name, NULL);
            
            if (!target_sym) {
                sem_error(ctx, node, "Undefined variable '%s'", wn->var_name);
            }

            if (wn->wash_type == 2) { 
                if (ctx->in_wash_block == 0) {
                    sem_error(ctx, node, "Cannot untaint outside a wash/clean block");
                } else {
                    if (target_sym) {
                        target_sym->is_pristine = 1; 
                    }
                }
            } else { 
                sem_scope_enter(ctx, 0, (VarType){0});
                
                if (wn->err_name) {
                    VarType err_type = {TYPE_INT, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0}; 
                    SemSymbol *err_sym = sem_symbol_add(ctx, wn->err_name, SYM_VAR, err_type);
                    err_sym->is_initialized = 1;
                }
                
                sem_check_block(ctx, wn->body);
                sem_scope_exit(ctx);
                
                if (wn->else_body) {
                    ctx->in_wash_block++;
                    sem_scope_enter(ctx, 0, (VarType){0});
                    
                    if (wn->else_body->type == NODE_WASH || wn->else_body->type == NODE_IF) {
                        sem_check_node(ctx, wn->else_body);
                    } else {
                        sem_check_block(ctx, wn->else_body);
                    }
                    
                    sem_scope_exit(ctx);
                    ctx->in_wash_block--;
                }
            }
            break;
        }
        case NODE_IF: {
            IfNode *ifn = (IfNode*)node;
            sem_check_expr(ctx, ifn->condition);
            
            sem_scope_enter(ctx, 0, (VarType){0});
            sem_check_block(ctx, ifn->then_body);
            sem_scope_exit(ctx);
            
            if (ifn->else_body) {
                sem_scope_enter(ctx, 0, (VarType){0});
                if (ifn->else_body->type == NODE_IF) {
                    sem_check_node(ctx, ifn->else_body);
                } else {
                    sem_check_block(ctx, ifn->else_body);
                }
                sem_scope_exit(ctx);
            }
            break;
        }
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode*)node;
            sem_check_expr(ctx, wn->condition);
            ctx->in_loop++;
            
            sem_scope_enter(ctx, 0, (VarType){0});
            sem_check_block(ctx, wn->body);
            sem_scope_exit(ctx);
            
            ctx->in_loop--;
            break;
        }
        case NODE_LOOP: {
            LoopNode *ln = (LoopNode*)node;
            sem_check_expr(ctx, ln->iterations);
            ctx->in_loop++;
            
            sem_scope_enter(ctx, 0, (VarType){0});
            sem_check_block(ctx, ln->body);
            sem_scope_exit(ctx);
            
            ctx->in_loop--;
            break;
        }
        case NODE_FOR_IN: {
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
            break;
        }
        case NODE_BREAK:
            if (ctx->in_loop == 0 && ctx->in_switch == 0) sem_error(ctx, node, "'break' outside loop or switch");
            break;
        case NODE_CONTINUE:
            if (ctx->in_loop == 0) sem_error(ctx, node, "'continue' outside loop");
            break;
        case NODE_CALL:
        case NODE_METHOD_CALL:
            sem_check_expr(ctx, node); 
            break;
        case NODE_EMIT: {
            EmitNode *en = (EmitNode*)node;
            sem_check_expr(ctx, en->value);
            break;
        }
        default: break;
    }
}

void sem_check_block(SemanticCtx *ctx, ASTNode *block) {
    ASTNode *curr = block;
    while (curr) {
        sem_check_node(ctx, curr);
        curr = curr->next;
    }
}

void sem_check_node(SemanticCtx *ctx, ASTNode *node) {
    if (node->type == NODE_FUNC_DEF) sem_check_func_def(ctx, (FuncDefNode*)node);
    else if (node->type == NODE_CLASS) {
        ClassNode *cn = (ClassNode*)node;
        SemSymbol *sym = sem_symbol_lookup(ctx, cn->name, NULL);
        if (sym && sym->inner_scope) {
            SemScope *old = ctx->current_scope;
            ctx->current_scope = sym->inner_scope;
            
            ASTNode *mem = cn->members;
            while(mem) {
                if (mem->type == NODE_FUNC_DEF) sem_check_func_def(ctx, (FuncDefNode*)mem);
                else if (mem->type == NODE_VAR_DECL) sem_check_var_decl(ctx, (VarDeclNode*)mem, 0); 
                mem = mem->next;
            }
            
            ctx->current_scope = old;
        }
    }
    else if (node->type == NODE_NAMESPACE) {
        NamespaceNode *ns = (NamespaceNode*)node;
        SemSymbol *sym = sem_symbol_lookup(ctx, ns->name, NULL);
        if (sym && sym->inner_scope) {
            SemScope *old = ctx->current_scope;
            ctx->current_scope = sym->inner_scope;
            sem_check_block(ctx, ns->body);
            ctx->current_scope = old;
        }
    }
    else if (node->type == NODE_VAR_DECL) {
        sem_check_var_decl(ctx, (VarDeclNode*)node, 1);
    }
    else {
        sem_check_stmt(ctx, node);
    }
}

void sem_check_func_def(SemanticCtx *ctx, FuncDefNode *node) {
    if (!ctx->compiler_ctx || !ctx->compiler_ctx->arena) return;

    sem_scope_enter(ctx, 1, node->ret_type);
    
    SemSymbol *old_func = ctx->current_func_sym;
    ctx->current_func_sym = sem_symbol_lookup(ctx, node->name, NULL);

    if (node->class_name) {
        VarType this_type = {TYPE_CLASS, 1, 0, arena_strdup(ctx->compiler_ctx->arena, node->class_name), 0, NULL, NULL, 0, 0, 0, 0}; 
        sem_symbol_add(ctx, "this", SYM_VAR, this_type);
    }

    Parameter *p = node->params;
    while (p) {
        if (p->name) {
            SemSymbol *s = sem_symbol_add(ctx, p->name, SYM_VAR, p->type);
            s->is_initialized = 1;
        }
        p = p->next;
    }
    
    sem_check_block(ctx, node->body);
    sem_scope_exit(ctx);
    
    ctx->current_func_sym = old_func;
}
