#include "semantic.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// TODO simplify this
VarType check_expr(SemCtx *ctx, ASTNode *node) {
    VarType unknown = {TYPE_UNKNOWN, 0, NULL};
    if (!node) return unknown;

    switch(node->type) {
        case NODE_LITERAL:
            return ((LiteralNode*)node)->var_type;
        
        case NODE_ARRAY_LIT: {
            ArrayLitNode *an = (ArrayLitNode*)node;
            if (!an->elements) {
                VarType t = {TYPE_UNKNOWN, 0, NULL, 1}; 
                return t;
            }
            VarType first_t = check_expr(ctx, an->elements);
            ASTNode *curr = an->elements->next;
            int count = 1;
            while(curr) {
                VarType t = check_expr(ctx, curr);
                if (!are_types_equal(first_t, t)) {
                    sem_error(ctx, curr, "Array element type mismatch. Expected '%s', got '%s'", 
                              type_to_str(first_t), type_to_str(t));
                }
                curr = curr->next;
                count++;
            }
            VarType ret = first_t;
            ret.array_size = count; 
            return ret;
        }

        case NODE_VAR_REF: {
            char *name = ((VarRefNode*)node)->name;
            SemSymbol *sym = find_symbol_semantic(ctx, name);
            if (!sym) {
                if (strcmp(name, "this") == 0) {
                    if (!ctx->current_class) {
                        sem_error(ctx, node, "'this' used outside of class method");
                        return unknown;
                    }
                    VarType t = {TYPE_CLASS, 1, strdup(ctx->current_class)}; 
                    return t;
                }
                
                // Check for Implicit 'this' member access
                if (ctx->current_class) {
                    SemSymbol *mem = find_member(ctx, ctx->current_class, name);
                    if (mem) {
                        return mem->type;
                    }
                }

                if (find_sem_enum(ctx, name)) {
                    sem_error(ctx, node, "'%s' is an Enum type, not a value. Use '%s.Member' or access members directly.", name, name);
                    return unknown;
                }
                
                sem_error(ctx, node, "Undefined symbol '%s'", name);
                
                const char *guess = find_closest_var_name(ctx, name);
                if (guess) sem_suggestion(ctx, node, guess);
                
                return unknown;
            }
            VarType res = sym->type;
            if (sym->is_array) {
                 res.array_size = sym->array_size > 0 ? sym->array_size : 1; 
            }
            return res;
        }

        case NODE_BINARY_OP: {
            BinaryOpNode *op = (BinaryOpNode*)node;
            VarType l = check_expr(ctx, op->left);
            VarType r = check_expr(ctx, op->right);
            
            if (l.base == TYPE_UNKNOWN || r.base == TYPE_UNKNOWN) return unknown;

            if (l.base == TYPE_STRING && r.base == TYPE_STRING && l.ptr_depth == 0 && r.ptr_depth == 0) {
                 if (op->op == TOKEN_PLUS) return l; 
                 if (op->op == TOKEN_EQ || op->op == TOKEN_NEQ || 
                     op->op == TOKEN_LT || op->op == TOKEN_GT || 
                     op->op == TOKEN_LTE || op->op == TOKEN_GTE) {
                     return (VarType){TYPE_BOOL, 0, NULL};
                 }
            }

            if (!are_types_equal(l, r)) {
                if (!((l.base == TYPE_INT || l.base == TYPE_FLOAT || l.base == TYPE_DOUBLE) && 
                      (r.base == TYPE_INT || r.base == TYPE_FLOAT || r.base == TYPE_DOUBLE))) {
                    sem_error(ctx, node, "Type mismatch in binary operation: '%s' vs '%s'", type_to_str(l), type_to_str(r));
                }
            }
            if (op->op == TOKEN_LT || op->op == TOKEN_GT || op->op == TOKEN_EQ || op->op == TOKEN_NEQ || op->op == TOKEN_LTE || op->op == TOKEN_GTE) {
                VarType bool_t = {TYPE_BOOL, 0, NULL};
                return bool_t;
            }
            return l;
        }

        case NODE_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode*)node;
            VarType t = check_expr(ctx, u->operand);
            if (t.base == TYPE_UNKNOWN) return unknown;

            if (u->op == TOKEN_MINUS || u->op == TOKEN_BIT_NOT) {
                if (t.base == TYPE_INT || t.base == TYPE_FLOAT || t.base == TYPE_DOUBLE) return t;
                sem_error(ctx, node, "Invalid operand type '%s' for unary operator", type_to_str(t));
                return unknown;
            }
            if (u->op == TOKEN_NOT) {
                // Logical NOT
                return (VarType){TYPE_BOOL, 0, NULL};
            }
            if (u->op == TOKEN_STAR) {
                // Dereference
                if (t.ptr_depth > 0) {
                    t.ptr_depth--;
                    return t;
                }
                sem_error(ctx, node, "Cannot dereference non-pointer type '%s'", type_to_str(t));
                return unknown;
            }
            if (u->op == TOKEN_AND) {
                // Address of
                t.ptr_depth++;
                return t;
            }
            return t;
        }

        case NODE_ASSIGN: {
            AssignNode *a = (AssignNode*)node;
            VarType l_type = unknown;
            int is_const = 0;
            
            if (a->name) {
                SemSymbol *sym = find_symbol_semantic(ctx, a->name);
                if (!sym) {
                    if (ctx->current_class) {
                         SemSymbol *mem = find_member(ctx, ctx->current_class, a->name);
                         if (mem) {
                             l_type = mem->type;
                             is_const = !mem->is_mutable;
                         } else {
                             sem_error(ctx, node, "Assignment to undefined symbol '%s'", a->name);
                             const char *guess = find_closest_var_name(ctx, a->name);
                             if (guess) sem_suggestion(ctx, node, guess);
                         }
                    } else {
                        sem_error(ctx, node, "Assignment to undefined symbol '%s'", a->name);
                        const char *guess = find_closest_var_name(ctx, a->name);
                        if (guess) sem_suggestion(ctx, node, guess);
                    }
                } else {
                    l_type = sym->type;
                    is_const = !sym->is_mutable;
                }
            } else if (a->target) {
                l_type = check_expr(ctx, a->target);
            }
            
            if (is_const) {
                sem_error(ctx, node, "Cannot assign to immutable variable '%s'", a->name ? a->name : "target");
            }

            VarType r_type = check_expr(ctx, a->value);
            
            if (l_type.base != TYPE_UNKNOWN && r_type.base != TYPE_UNKNOWN) {
                if (!are_types_equal(l_type, r_type)) {
                    int cost = get_conversion_cost(r_type, l_type);
                    int compatible = 0;
                    if (cost != -1) compatible = 1;
                    if (l_type.ptr_depth > 0 && r_type.array_size > 0 && l_type.base == r_type.base) compatible = 1;
                    
                    if (compatible) {
                         // Warn on implicit casting if cost > 0
                         if (cost > 0) {
                             sem_info(ctx, node, "Implicit conversion from '%s' to '%s'", 
                                      type_to_str(r_type), type_to_str(l_type));

                             // Inject CastNode into AST
                             CastNode *cast = calloc(1, sizeof(CastNode));
                             cast->base.type = NODE_CAST;
                             cast->base.line = a->value->line;
                             cast->base.col = a->value->col;
                             cast->var_type = l_type;
                             cast->operand = a->value;
                             a->value = (ASTNode*)cast;
                         }
                    } else {
                         sem_error(ctx, node, "Type mismatch in assignment. Expected '%s', got '%s'", type_to_str(l_type), type_to_str(r_type));
                    }
                }
            }
            return l_type;
        }

        case NODE_INC_DEC: {
            IncDecNode *id = (IncDecNode*)node;
            VarType t = check_expr(ctx, id->target);
            
            // Check if l-value is mutable
            if (id->target->type == NODE_VAR_REF) {
                SemSymbol *sym = find_symbol_semantic(ctx, ((VarRefNode*)id->target)->name);
                if (sym && !sym->is_mutable) {
                    sem_error(ctx, node, "Cannot modify immutable variable '%s'", sym->name);
                }
            }
            
            if (t.base != TYPE_INT && t.base != TYPE_FLOAT && t.base != TYPE_DOUBLE && t.base != TYPE_CHAR) {
                 sem_error(ctx, node, "Increment/decrement requires numeric type, got '%s'", type_to_str(t));
            }
            return t;
        }

        case NODE_CALL: {
            CallNode *c = (CallNode*)node;
            ASTNode *arg = c->args;
            while(arg) { check_expr(ctx, arg); arg = arg->next; }

            // Builtins
            if (strcmp(c->name, "print") == 0 || strcmp(c->name, "printf") == 0) return (VarType){TYPE_VOID, 0, NULL};
            if (strcmp(c->name, "input") == 0) return (VarType){TYPE_STRING, 0, NULL};
            if (strcmp(c->name, "malloc") == 0 || strcmp(c->name, "alloc") == 0) return (VarType){TYPE_VOID, 1, NULL};
            if (strcmp(c->name, "free") == 0) return (VarType){TYPE_VOID, 0, NULL};
            if (strcmp(c->name, "setjmp") == 0) return (VarType){TYPE_INT, 0, NULL};
            if (strcmp(c->name, "longjmp") == 0) return (VarType){TYPE_VOID, 0, NULL};

            SemFunc *match = resolve_overload(ctx, node, c->name, c->args);
            if (match) {
                c->mangled_name = strdup(match->mangled_name);
                return match->ret_type;
            }
            
            // Check if class constructor
            SemClass *cls = ctx->classes;
            int is_cls = 0;
            while(cls) { if(strcmp(cls->name, c->name) == 0) { is_cls=1; break; } cls = cls->next; }
            if (is_cls) {
                SemClass *sc = find_sem_class(ctx, c->name);
                if (sc && sc->is_extern) {
                    sem_error(ctx, node, "Cannot instantiate extern opaque struct/class '%s' by calling a constructor", c->name);
                    return unknown;
                }
                return (VarType){TYPE_CLASS, 0, strdup(c->name)};
            }

            sem_error(ctx, node, "No matching overload for function '%s'", c->name);
            const char *type_guess = find_closest_type_name(ctx, c->name);
            const char *func_guess = find_closest_func_name(ctx, c->name);
            
            if (type_guess) {
                char hint_msg[256];
                snprintf(hint_msg, sizeof(hint_msg), "'%s' looks like type '%s'. Did you mean to declare a variable?", c->name, type_guess);
                sem_hint(ctx, node, hint_msg);
            } else if (func_guess) {
                sem_suggestion(ctx, node, func_guess);
            }

            return unknown;
        }
        
        case NODE_METHOD_CALL: {
            MethodCallNode *mc = (MethodCallNode*)node;
            ASTNode *arg = mc->args;
            while(arg) { check_expr(ctx, arg); arg = arg->next; }

            if (mc->object->type == NODE_VAR_REF) {
                char *name = ((VarRefNode*)mc->object)->name;
                if (!find_symbol_semantic(ctx, name)) {
                     char qualified[512];
                     snprintf(qualified, sizeof(qualified), "%s.%s", name, mc->method_name);
                     
                     SemFunc *ns_func = resolve_overload(ctx, node, qualified, mc->args);
                     if (ns_func) {
                         mc->mangled_name = strdup(ns_func->mangled_name);
                         mc->is_static = 1; 
                         return ns_func->ret_type;
                     }
                }
            }

            VarType obj_t = {TYPE_UNKNOWN, 0, NULL};

            if (mc->object->type == NODE_VAR_REF) {
                obj_t = check_expr(ctx, mc->object); 
            } else {
                obj_t = check_expr(ctx, mc->object);
            }
            
            if (obj_t.base == TYPE_CLASS && obj_t.class_name) {
                char *owner = NULL;
                SemFunc *f = resolve_method_in_hierarchy(ctx, node, obj_t.class_name, mc->method_name, mc->args, &owner);
                
                if (f) {
                    mc->mangled_name = strdup(f->mangled_name);
                    mc->owner_class = owner; 
                    return f->ret_type;
                } else {
                    sem_error(ctx, node, "Method '%s' not found in class '%s' (or parents/traits)", mc->method_name, obj_t.class_name);
                }
            } else {
                if (obj_t.base != TYPE_UNKNOWN) {
                    sem_error(ctx, node, "Method call on non-class type '%s'", type_to_str(obj_t));
                }
            }

            return unknown; 
        }
        
        case NODE_TRAIT_ACCESS: {
            TraitAccessNode *ta = (TraitAccessNode*)node;
            VarType obj_t = check_expr(ctx, ta->object);
            
            if (obj_t.base != TYPE_CLASS || !obj_t.class_name) {
                sem_error(ctx, node, "Trait access requires class object, got '%s'", type_to_str(obj_t));
                return unknown;
            }
            
            if (!class_has_trait(ctx, obj_t.class_name, ta->trait_name)) {
                sem_error(ctx, node, "Class '%s' does not implement trait '%s'", obj_t.class_name, ta->trait_name);
                return unknown;
            }
            
            VarType trait_type = {TYPE_CLASS, obj_t.ptr_depth, strdup(ta->trait_name)};
            trait_type.array_size = obj_t.array_size;
            return trait_type;
        }

        case NODE_ARRAY_ACCESS: {
            ArrayAccessNode *aa = (ArrayAccessNode*)node;
            
            if (aa->target->type == NODE_VAR_REF) {
                 char *name = ((VarRefNode*)aa->target)->name;
                 SemEnum *se = find_sem_enum(ctx, name);
                 if (se) {
                     VarType idx_t = check_expr(ctx, aa->index);
                     if (idx_t.base != TYPE_INT) sem_error(ctx, aa->index, "Enum string lookup requires integer index");
                     return (VarType){TYPE_STRING, 0, NULL};
                 }
            }

            VarType target_t = check_expr(ctx, aa->target);
            VarType idx_t = check_expr(ctx, aa->index);
            
            if (idx_t.base != TYPE_INT) {
                sem_error(ctx, node, "Array index must be an integer, got '%s'", type_to_str(idx_t));
            }
            
            if (target_t.base == TYPE_STRING && target_t.ptr_depth == 0) {
                 return (VarType){TYPE_CHAR, 0, NULL};
            }

            if (aa->index->type == NODE_LITERAL) {
                int idx = ((LiteralNode*)aa->index)->val.int_val;
                if (aa->target->type == NODE_VAR_REF) {
                    SemSymbol *sym = find_symbol_semantic(ctx, ((VarRefNode*)aa->target)->name);
                    if (sym && sym->is_array && sym->array_size > 0) {
                        if (idx < 0 || idx >= sym->array_size) {
                            sem_error(ctx, node, "Array index %d out of bounds (size %d)", idx, sym->array_size);
                        }
                    }
                }
            }
            
            if (target_t.ptr_depth > 0) target_t.ptr_depth--;
            else if (target_t.array_size > 0) {
                 target_t.array_size = 0; 
            }
            return target_t;
        }
        
        case NODE_MEMBER_ACCESS: {
             MemberAccessNode *ma = (MemberAccessNode*)node;
             
             if (ma->object->type == NODE_VAR_REF) {
                 char *name = ((VarRefNode*)ma->object)->name;
                 SemEnum *se = find_sem_enum(ctx, name);
                 if (se) {
                     int found = 0;
                     struct SemEnumMember *m = se->members;
                     while(m) { if(strcmp(m->name, ma->member_name) == 0) { found=1; break; } m=m->next; }
                     if (!found) sem_error(ctx, node, "Enum '%s' has no member '%s'", name, ma->member_name);
                     
                     return (VarType){TYPE_INT, 0, NULL};
                 }
             }

             VarType obj_t = check_expr(ctx, ma->object);
             
             if (obj_t.base == TYPE_UNKNOWN) return unknown;

             if (obj_t.ptr_depth > 0) obj_t.ptr_depth--;
             
             if (obj_t.base == TYPE_CLASS && obj_t.class_name) {
                 SemSymbol *mem = find_member(ctx, obj_t.class_name, ma->member_name);
                 if (mem) {
                     return mem->type;
                 } else {
                     sem_error(ctx, node, "Class '%s' has no member '%s'", obj_t.class_name, ma->member_name);
                 }
             }
             
             VarType ret = {TYPE_UNKNOWN, 0, NULL};
             return ret;
        }
        
        case NODE_HAS_METHOD:
        case NODE_HAS_ATTRIBUTE: {
            UnaryOpNode *u = (UnaryOpNode*)node;
            VarType t = check_expr(ctx, u->operand);
            if (t.base != TYPE_CLASS || !t.class_name) {
                sem_error(ctx, node, "hasmethod/hasattribute requires a class instance or type.");
                return unknown;
            }
            // Return array of strings (string[] -> char**)
            // In Alkyl, TYPE_STRING is char*. Array is size > 0.
            // But here the size is unknown at compile time (or rather, fixed but we just return pointer to start)
            VarType ret = {TYPE_STRING, 0, NULL};
            ret.array_size = 0; // Treated as pointer to array decay
            ret.ptr_depth = 1; // char**
            return ret;
        }
        
        case NODE_TYPEOF: {
            UnaryOpNode *u = (UnaryOpNode*)node;
            check_expr(ctx, u->operand);
            return (VarType){TYPE_STRING, 0, NULL};
        }

        case NODE_CAST: {
            CastNode *cn = (CastNode*)node;
            cn->var_type = resolve_typedef(ctx, cn->var_type);
            VarType from = check_expr(ctx, cn->operand);
            VarType to = cn->var_type;
            
            // Check validity of cast
            int cost = get_conversion_cost(from, to);
            // Also allow explicit casts for things that implicit doesn't allow (like double -> int cost 2)
            // or pointer casts
            
            if (cost == -1) {
                // Check pointer casts
                if (from.ptr_depth > 0 && to.ptr_depth > 0) {
                    // Pointer to Pointer is usually allowed explicitly
                } else if (from.ptr_depth > 0 && to.base == TYPE_INT) {
                    // Ptr to Int
                } else if (from.base == TYPE_INT && to.ptr_depth > 0) {
                    // Int to Ptr
                } else {
                     // Still warn/error if completely incompatible? 
                     // For now, we trust the explicit cast mostly, but could warn.
                }
            }
            
            return to;
        }

        default:
            return unknown;
    }
}

void check_block(SemCtx *ctx, ASTNode *node) {
    while (node) {
        check_stmt(ctx, node);
        node = node->next;
    }
}

void check_stmt(SemCtx *ctx, ASTNode *node) {
    if (!node) return;
    
    switch(node->type) {
        case NODE_VAR_DECL: {
            VarDeclNode *vd = (VarDeclNode*)node;
            vd->var_type = resolve_typedef(ctx, vd->var_type);
            
            // Check opaque struct allocation by value
            if (vd->var_type.base == TYPE_CLASS && vd->var_type.ptr_depth == 0) {
                SemClass *cls = find_sem_class(ctx, vd->var_type.class_name);
                if (cls && cls->is_extern) {
                     sem_error(ctx, node, "Cannot allocate extern opaque type '%s' by value. Use pointer (e.g. %s*).", cls->name, cls->name);
                }
            }

            SemSymbol *existing = find_symbol_current_scope(ctx, vd->name);
            if (existing) {
                sem_error(ctx, node, "Redefinition of variable '%s' in current scope", vd->name);
                if (existing->decl_line > 0) {
                    sem_reason(ctx, existing->decl_line, existing->decl_col, "Previous definition of '%s' was here", vd->name);
                }
            } else {
                Scope *s = ctx->current_scope->parent;
                SemSymbol *shadowed = NULL;
                const char *shadow_type = "outer scope";
                
                while (s) {
                    SemSymbol *sym = s->symbols;
                    while (sym) {
                        if (strcmp(sym->name, vd->name) == 0) {
                            shadowed = sym;
                            break;
                        }
                        sym = sym->next;
                    }
                    if (shadowed) break;
                    s = s->parent;
                }

                if (!shadowed && ctx->current_class) {
                    SemSymbol *mem = find_member(ctx, ctx->current_class, vd->name);
                    if (mem) {
                        shadowed = mem;
                        shadow_type = "class member";
                    }
                }

                if (shadowed) {
                    sem_info(ctx, node, "Variable '%s' shadows a variable in %s", vd->name, shadow_type);
                    if (shadowed->decl_line > 0) {
                        sem_reason(ctx, shadowed->decl_line, shadowed->decl_col, "Shadowed declaration is here");
                    }
                }
            }
            
            VarType inferred = vd->var_type;
            if (vd->var_type.base == TYPE_AUTO) {
                if (!vd->initializer) {
                    sem_error(ctx, node, "Cannot infer type for '%s' without initializer", vd->name);
                    inferred.base = TYPE_INT; 
                } else {
                    inferred = check_expr(ctx, vd->initializer);
                }
            } else if (vd->initializer) {
                VarType init_t = check_expr(ctx, vd->initializer);
                if (!are_types_equal(vd->var_type, init_t)) {
                     int cost = get_conversion_cost(init_t, vd->var_type);
                     int ok = 0;
                     if (cost != -1) ok = 1;
                     
                     if (vd->var_type.base == TYPE_STRING && init_t.base == TYPE_STRING) ok = 1;
                     if (vd->var_type.base == TYPE_CHAR && vd->is_array && init_t.base == TYPE_STRING) ok = 1;
                     if (vd->var_type.base == TYPE_CHAR && vd->var_type.ptr_depth == 1 && init_t.base == TYPE_STRING) ok = 1;
                     
                     if (ok) {
                         // Print info for implicit conversion (widening or narrowing)
                         if (cost > 0) {
                             sem_info(ctx, node, "Implicit conversion from '%s' to '%s'", 
                                      type_to_str(init_t), type_to_str(vd->var_type));

                             // Inject CastNode into AST
                             CastNode *cast = calloc(1, sizeof(CastNode));
                             cast->base.type = NODE_CAST;
                             cast->base.line = vd->initializer->line;
                             cast->base.col = vd->initializer->col;
                             cast->var_type = vd->var_type;
                             cast->operand = vd->initializer;
                             vd->initializer = (ASTNode*)cast;
                         }
                     } else {
                        sem_error(ctx, node, "Variable '%s' type mismatch. Declared '%s', init '%s'", 
                                  vd->name, type_to_str(vd->var_type), type_to_str(init_t));
                     }
                }
            }
            
            int arr_size = 0;
            if (vd->is_array) {
                if (vd->array_size && vd->array_size->type == NODE_LITERAL) {
                    arr_size = ((LiteralNode*)vd->array_size)->val.int_val;
                } else if (vd->initializer && vd->initializer->type == NODE_LITERAL && ((LiteralNode*)vd->initializer)->var_type.base == TYPE_STRING) {
                     arr_size = strlen(((LiteralNode*)vd->initializer)->val.str_val) + 1;
                } else if (vd->initializer && vd->initializer->type == NODE_ARRAY_LIT) {
                     ASTNode* el = ((ArrayLitNode*)vd->initializer)->elements;
                     while(el) { arr_size++; el = el->next; }
                }
            }

            add_symbol_semantic(ctx, vd->name, inferred, vd->is_mutable, vd->is_array, arr_size, node->line, node->col);
            break;
        }

        case NODE_RETURN: {
            ReturnNode *r = (ReturnNode*)node;
            VarType ret_t = {TYPE_VOID, 0, NULL};
            if (r->value) ret_t = check_expr(ctx, r->value);
            
            if (!are_types_equal(ctx->current_func_ret_type, ret_t)) {
                int cost = get_conversion_cost(ret_t, ctx->current_func_ret_type);
                if (cost != -1) {
                    if (cost > 0 && r->value) {
                         // Inject CastNode into AST for implicit return conversion
                         CastNode *cast = calloc(1, sizeof(CastNode));
                         cast->base.type = NODE_CAST;
                         cast->base.line = r->value->line;
                         cast->base.col = r->value->col;
                         cast->var_type = ctx->current_func_ret_type;
                         cast->operand = r->value;
                         r->value = (ASTNode*)cast;
                    }
                } else {
                    sem_error(ctx, node, "Return type mismatch. Expected '%s', got '%s'", 
                              type_to_str(ctx->current_func_ret_type), type_to_str(ret_t));
                }
            }
            break;
        }

        case NODE_IF: {
            IfNode *i = (IfNode*)node;
            check_expr(ctx, i->condition);
            enter_scope(ctx);
            check_block(ctx, i->then_body);
            exit_scope(ctx);
            if (i->else_body) {
                enter_scope(ctx);
                check_block(ctx, i->else_body);
                exit_scope(ctx);
            }
            break;
        }

        case NODE_SWITCH: {
            SwitchNode *s = (SwitchNode*)node;
            check_expr(ctx, s->condition);
            
            enter_scope(ctx);
            int prev_loop = ctx->in_loop;
            ctx->in_loop = 1; 
            
            ASTNode *c = s->cases;
            while(c) {
                CaseNode *cn = (CaseNode*)c;
                check_expr(ctx, cn->value);
                check_block(ctx, cn->body);
                c = c->next;
            }
            if (s->default_case) check_block(ctx, s->default_case);
            
            ctx->in_loop = prev_loop;
            exit_scope(ctx);
            break;
        }

        case NODE_LOOP: {
            LoopNode *l = (LoopNode*)node;
            check_expr(ctx, l->iterations);
            int prev_loop = ctx->in_loop;
            ctx->in_loop = 1;
            enter_scope(ctx);
            check_block(ctx, l->body);
            exit_scope(ctx);
            ctx->in_loop = prev_loop;
            break;
        }
        
        case NODE_WHILE: {
            WhileNode *w = (WhileNode*)node;
            check_expr(ctx, w->condition);
            int prev_loop = ctx->in_loop;
            ctx->in_loop = 1;
            enter_scope(ctx);
            check_block(ctx, w->body);
            exit_scope(ctx);
            ctx->in_loop = prev_loop;
            break;
        }

        case NODE_BREAK:
        case NODE_CONTINUE:
            if (!ctx->in_loop) {
                sem_error(ctx, node, "'break' or 'continue' used outside of loop or switch");
            }
            break;

        case NODE_FUNC_DEF:
            break;

        default:
            check_expr(ctx, node); 
            break;
    }
}
