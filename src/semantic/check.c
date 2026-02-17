#include "semantic.h"
#include "../diagnostic/diagnostic.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// --- Internal Helper: Local Lookup ---
// Used to detect redeclarations in the same scope
SemSymbol* lookup_local_symbol(SemanticCtx *ctx, const char *name) {
    if (!ctx->current_scope) return NULL;
    SemSymbol *sym = ctx->current_scope->symbols;
    while (sym) {
        if (strcmp(sym->name, name) == 0) return sym;
        sym = sym->next;
    }
    return NULL;
}

// --- Standardized Error Reporting ---
void sem_error(SemanticCtx *ctx, ASTNode *node, const char *fmt, ...) {
    ctx->error_count++;
    
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (ctx->current_source && node) {
        // Create a temporary lexer to utilize the diagnostic system's 
        // source snippet printing capabilities.
        Lexer l;
        lexer_init(&l, ctx->current_source);
        
        // Construct a token representing the node's location
        Token t;
        t.line = node->line;
        t.col = node->col;
        t.type = TOKEN_UNKNOWN; // Type irrelevant for error reporting
        t.text = NULL;
        t.int_val = 0; 
        t.double_val = 0.0;
        
        report_error(&l, t, msg);
    } else {
        // Fallback if source is not available
        if (node) {
            fprintf(stderr, "[Semantic Error] Line %d, Col %d: %s\n", node->line, node->col, msg);
        } else {
            fprintf(stderr, "[Semantic Error] %s\n", msg);
        }
    }
}

// --- Type Helpers ---

int is_numeric(VarType t) {
    return (t.base >= TYPE_INT && t.base <= TYPE_LONG_DOUBLE && t.ptr_depth == 0);
}

int is_integer(VarType t) {
    return (t.base >= TYPE_INT && t.base <= TYPE_CHAR && t.ptr_depth == 0);
}

int is_bool(VarType t) {
    return (t.base == TYPE_BOOL && t.ptr_depth == 0);
}

int is_pointer(VarType t) {
    return t.ptr_depth > 0 || t.array_size > 0 || t.base == TYPE_STRING || t.is_func_ptr;
}

// --- Pass 1: Header Scanning (Register Types & Functions) ---

void sem_scan_class_members(SemanticCtx *ctx, ClassNode *cn, SemSymbol *class_sym) {
    // Manually create a scope for the class members
    SemScope *class_scope = malloc(sizeof(SemScope));
    class_scope->symbols = NULL;
    class_scope->parent = ctx->current_scope; // Parent is the scope where class is defined
    class_scope->is_function_scope = 0;
    class_scope->expected_ret_type = (VarType){0};
    
    // Link to symbol
    class_sym->inner_scope = class_scope;
    
    // Temporarily enter class scope
    SemScope *old_scope = ctx->current_scope;
    ctx->current_scope = class_scope;
    
    ASTNode *mem = cn->members;
    while(mem) {
        if (mem->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)mem;
            // Class fields are symbols in the class scope
            sem_symbol_add(ctx, vd->name, SYM_VAR, vd->var_type);
        } else if (mem->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)mem;
            // Methods are symbols in the class scope
            sem_symbol_add(ctx, fd->name, SYM_FUNC, fd->ret_type);
        }
        mem = mem->next;
    }
    
    // Restore scope
    ctx->current_scope = old_scope;
}

void sem_scan_top_level(SemanticCtx *ctx, ASTNode *node) {
    while (node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            sem_symbol_add(ctx, fd->name, SYM_FUNC, fd->ret_type);
        }
        else if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            SemSymbol *sym = sem_symbol_add(ctx, cn->name, SYM_CLASS, (VarType){TYPE_CLASS, 0, strdup(cn->name)});
            // Scan internal members immediately so checking phase can resolve them
            sem_scan_class_members(ctx, cn, sym);
        }
        else if (node->type == NODE_ENUM) {
            EnumNode *en = (EnumNode*)node;
            sem_symbol_add(ctx, en->name, SYM_ENUM, (VarType){TYPE_INT, 0, NULL}); // Enums act like Ints
            
            // Also register enum members as global constants (optional, depends on language rules)
            // Or we can rely on EnumName.Member access
            // For now, let's register the Enum Type name.
        }
        else if (node->type == NODE_NAMESPACE) {
            NamespaceNode *ns = (NamespaceNode*)node;
            // Namespaces act like scopes. 
            SemSymbol *sym = sem_symbol_add(ctx, ns->name, SYM_NAMESPACE, (VarType){TYPE_VOID});
            
            SemScope *ns_scope = malloc(sizeof(SemScope));
            ns_scope->symbols = NULL;
            ns_scope->parent = ctx->current_scope;
            sym->inner_scope = ns_scope;
            
            SemScope *old = ctx->current_scope;
            ctx->current_scope = ns_scope;
            sem_scan_top_level(ctx, ns->body);
            ctx->current_scope = old;
        }
        node = node->next;
    }
}

// --- Pass 2: Checking ---

void sem_check_var_decl(SemanticCtx *ctx, VarDeclNode *node) {
    // 1. Check Initializer
    if (node->initializer) {
        sem_check_expr(ctx, node->initializer);
        VarType init_type = sem_get_node_type(ctx, node->initializer);
        
        // 2. Inference (let / auto)
        if (node->var_type.base == TYPE_AUTO) {
            if (init_type.base == TYPE_UNKNOWN) {
                sem_error(ctx, (ASTNode*)node, "Cannot infer type for variable '%s' (unknown initializer type)", node->name);
            } else if (init_type.base == TYPE_VOID) {
                sem_error(ctx, (ASTNode*)node, "Cannot infer type 'void' for variable '%s'", node->name);
            } else {
                node->var_type = init_type; 
                // We update the AST here so later passes don't see AUTO, 
                // though strictly the Side Table holds the truth.
            }
        } 
        // 3. Compatibility Check
        else {
            if (!sem_types_are_compatible(node->var_type, init_type)) {
                char *t1 = sem_type_to_str(node->var_type);
                char *t2 = sem_type_to_str(init_type);
                sem_error(ctx, (ASTNode*)node, "Type mismatch in declaration of '%s'. Expected '%s', got '%s'", node->name, t1, t2);
            }
        }
    } else {
        if (node->var_type.base == TYPE_AUTO) {
            sem_error(ctx, (ASTNode*)node, "Variable '%s' declared 'let' but has no initializer", node->name);
        }
    }

    // 4. Register Symbol
    // Use local lookup to prevent redeclaration in the exact same scope
    if (lookup_local_symbol(ctx, node->name)) {
        sem_error(ctx, (ASTNode*)node, "Redeclaration of variable '%s' in the same scope", node->name);
    } else {
        sem_symbol_add(ctx, node->name, SYM_VAR, node->var_type);
    }
}

void sem_check_assign(SemanticCtx *ctx, AssignNode *node) {
    // Check RHS
    sem_check_expr(ctx, node->value);
    VarType rhs_type = sem_get_node_type(ctx, node->value);
    VarType lhs_type;

    // Check LHS
    if (node->name) {
        SemSymbol *sym = sem_symbol_lookup(ctx, node->name);
        if (!sym) {
            sem_error(ctx, (ASTNode*)node, "Undefined variable '%s'", node->name);
            lhs_type = (VarType){TYPE_UNKNOWN};
        } else {
            if (!sym->is_mutable) {
                sem_error(ctx, (ASTNode*)node, "Cannot assign to immutable variable '%s'", node->name);
            }
            lhs_type = sym->type;
            
            // Handle array index on variable (e.g., arr[0] = 5)
            if (node->index) {
                sem_check_expr(ctx, node->index);
                VarType idx_t = sem_get_node_type(ctx, node->index);
                if (!is_integer(idx_t)) {
                    sem_error(ctx, node->index, "Array index must be an integer");
                }
                
                // Decay type
                if (lhs_type.array_size > 0) lhs_type.array_size = 0;
                else if (lhs_type.ptr_depth > 0) lhs_type.ptr_depth--;
                else {
                    sem_error(ctx, (ASTNode*)node, "Cannot index into non-array variable '%s'", node->name);
                }
            }
        }
    } else {
        // Complex Target (Member access, Array access, etc.)
        sem_check_expr(ctx, node->target);
        lhs_type = sem_get_node_type(ctx, node->target);
    }

    if (lhs_type.base != TYPE_UNKNOWN && rhs_type.base != TYPE_UNKNOWN) {
        if (!sem_types_are_compatible(lhs_type, rhs_type)) {
             char *t1 = sem_type_to_str(lhs_type);
             char *t2 = sem_type_to_str(rhs_type);
             sem_error(ctx, (ASTNode*)node, "Invalid assignment. Cannot assign '%s' to '%s'", t2, t1);
        }
    }
}

void sem_check_call(SemanticCtx *ctx, CallNode *node) {
    // Resolve Function
    SemSymbol *sym = sem_symbol_lookup(ctx, node->name);
    
    // Check if it's a class constructor
    if (!sym) {
        // Might be a class name (SYM_CLASS) handled below
    }
    
    if (!sym) {
        sem_error(ctx, (ASTNode*)node, "Undefined function or class '%s'", node->name);
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
        return;
    }
    
    // Validate Arguments
    // Note: For full robustness we need to store param types in SemSymbol.
    int arg_count = 0;
    ASTNode *arg = node->args;
    while(arg) {
        sem_check_expr(ctx, arg);
        arg = arg->next;
        arg_count++;
    }

    if (sym->kind == SYM_CLASS) {
        // Constructor call
        VarType instance = {TYPE_CLASS, 1, strdup(sym->name)}; // Pointer to class (instance)
        sem_set_node_type(ctx, (ASTNode*)node, instance);
    } else {
        // Function call
        sem_set_node_type(ctx, (ASTNode*)node, sym->type); // Return type
    }
}

void sem_check_binary_op(SemanticCtx *ctx, BinaryOpNode *node) {
    sem_check_expr(ctx, node->left);
    sem_check_expr(ctx, node->right);
    
    VarType l = sem_get_node_type(ctx, node->left);
    VarType r = sem_get_node_type(ctx, node->right);
    
    if (l.base == TYPE_UNKNOWN || r.base == TYPE_UNKNOWN) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
        return;
    }
    
    // Logic Ops (&&, ||) -> Bool
    if (node->op == TOKEN_AND_AND || node->op == TOKEN_OR_OR) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_BOOL});
        return;
    }
    
    // Comparison Ops -> Bool
    if (node->op == TOKEN_EQ || node->op == TOKEN_NEQ || 
        node->op == TOKEN_LT || node->op == TOKEN_GT || 
        node->op == TOKEN_LTE || node->op == TOKEN_GTE) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_BOOL});
        return;
    }
    
    // Arithmetic
    if (is_numeric(l) && is_numeric(r)) {
        // Promotion logic: Double > Float > Long > Int
        if (l.base == TYPE_LONG_DOUBLE || r.base == TYPE_LONG_DOUBLE) 
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_LONG_DOUBLE});
        else if (l.base == TYPE_DOUBLE || r.base == TYPE_DOUBLE) 
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_DOUBLE});
        else if (l.base == TYPE_FLOAT || r.base == TYPE_FLOAT) 
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_FLOAT});
        else if (l.base == TYPE_LONG || r.base == TYPE_LONG) 
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_LONG});
        else 
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_INT});
    } 
    // Pointer Arithmetic
    else if (is_pointer(l) && is_integer(r)) {
         sem_set_node_type(ctx, (ASTNode*)node, l);
    }
    else if (is_integer(l) && is_pointer(r)) {
         sem_set_node_type(ctx, (ASTNode*)node, r);
    }
    // String Concatenation
    else if (l.base == TYPE_STRING || r.base == TYPE_STRING) {
         if (node->op == TOKEN_PLUS) 
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_STRING});
         else {
            sem_error(ctx, (ASTNode*)node, "Invalid operation on strings");
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
         }
    }
    else {
        sem_error(ctx, (ASTNode*)node, "Invalid operands for binary operator");
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
    }
}

void sem_check_member_access(SemanticCtx *ctx, MemberAccessNode *node) {
    sem_check_expr(ctx, node->object);
    VarType obj_type = sem_get_node_type(ctx, node->object);
    
    if (obj_type.base == TYPE_UNKNOWN) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
        return;
    }
    
    // Check if object is a Class/Struct
    if (obj_type.base == TYPE_CLASS && obj_type.class_name) {
        SemSymbol *class_sym = sem_symbol_lookup(ctx, obj_type.class_name);
        if (!class_sym || class_sym->kind != SYM_CLASS) {
            sem_error(ctx, (ASTNode*)node, "Type '%s' is not a class/struct", obj_type.class_name);
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
            return;
        }
        
        // Look inside the class scope
        if (!class_sym->inner_scope) {
             sem_error(ctx, (ASTNode*)node, "Class '%s' has no members (incomplete definition?)", obj_type.class_name);
             sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
             return;
        }
        
        // Manual lookup in inner scope
        SemSymbol *member = class_sym->inner_scope->symbols;
        int found = 0;
        while (member) {
            if (strcmp(member->name, node->member_name) == 0) {
                sem_set_node_type(ctx, (ASTNode*)node, member->type);
                found = 1;
                break;
            }
            member = member->next;
        }
        
        if (!found) {
            sem_error(ctx, (ASTNode*)node, "Class '%s' has no member named '%s'", obj_type.class_name, node->member_name);
            sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
        }
    } 
    else if (obj_type.base == TYPE_STRING && strcmp(node->member_name, "length") == 0) {
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_INT});
    }
    else {
        sem_error(ctx, (ASTNode*)node, "Cannot access member on non-class type");
        sem_set_node_type(ctx, (ASTNode*)node, (VarType){TYPE_UNKNOWN});
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
            SemSymbol *sym = sem_symbol_lookup(ctx, ref->name);
            if (sym) {
                sem_set_node_type(ctx, node, sym->type);
            } else {
                sem_error(ctx, node, "Undefined variable '%s'", ref->name);
                sem_set_node_type(ctx, node, (VarType){TYPE_UNKNOWN});
            }
            break;
        }
        case NODE_BINARY_OP: sem_check_binary_op(ctx, (BinaryOpNode*)node); break;
        case NODE_UNARY_OP: {
            UnaryOpNode *un = (UnaryOpNode*)node;
            sem_check_expr(ctx, un->operand);
            VarType t = sem_get_node_type(ctx, un->operand);
            
            if (un->op == TOKEN_AND) { // Address Of
                t.ptr_depth++;
            } else if (un->op == TOKEN_STAR) { // Dereference
                if (t.ptr_depth > 0) t.ptr_depth--;
                else sem_error(ctx, node, "Cannot dereference non-pointer");
            } else if (un->op == TOKEN_NOT) {
                t = (VarType){TYPE_BOOL};
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
            
            VarType t = sem_get_node_type(ctx, aa->target);
            if (t.array_size > 0) t.array_size = 0;
            else if (t.ptr_depth > 0) t.ptr_depth--;
            else {
                sem_error(ctx, node, "Type is not an array or pointer");
                t = (VarType){TYPE_UNKNOWN};
            }
            sem_set_node_type(ctx, node, t);
            break;
        }
        case NODE_CAST: {
            CastNode *cn = (CastNode*)node;
            sem_check_expr(ctx, cn->operand);
            sem_set_node_type(ctx, node, cn->var_type);
            break;
        }
        case NODE_METHOD_CALL: {
            // Simplified check: treat as member access logic + call
            MethodCallNode *mc = (MethodCallNode*)node;
            sem_check_expr(ctx, mc->object);
            // TODO: Lookup method in class scope
            // For now, assume it returns Unknown or Void to prevent crash
            sem_set_node_type(ctx, node, (VarType){TYPE_VOID}); 
            break;
        }
        case NODE_ARRAY_LIT: {
            ArrayLitNode *al = (ArrayLitNode*)node;
            ASTNode *el = al->elements;
            VarType elem_type = {TYPE_UNKNOWN};
            if (el) {
                sem_check_expr(ctx, el);
                elem_type = sem_get_node_type(ctx, el);
                el = el->next;
            }
            while(el) {
                sem_check_expr(ctx, el);
                el = el->next;
            }
            // Array literal type is T* or T[]
            elem_type.ptr_depth++;
            sem_set_node_type(ctx, node, elem_type);
            break;
        }
        default: break;
    }
}

void sem_check_stmt(SemanticCtx *ctx, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_VAR_DECL: sem_check_var_decl(ctx, (VarDeclNode*)node); break;
        case NODE_ASSIGN: sem_check_assign(ctx, (AssignNode*)node); break;
        case NODE_RETURN: {
            ReturnNode *rn = (ReturnNode*)node;
            if (rn->value) {
                sem_check_expr(ctx, rn->value);
                VarType val = sem_get_node_type(ctx, rn->value);
                // Check against current function return type
                if (ctx->current_scope->is_function_scope) {
                    if (!sem_types_are_compatible(ctx->current_scope->expected_ret_type, val)) {
                        sem_error(ctx, node, "Return type mismatch");
                    }
                }
            } else {
                 if (ctx->current_scope->is_function_scope && ctx->current_scope->expected_ret_type.base != TYPE_VOID) {
                     sem_error(ctx, node, "Function must return a value");
                 }
            }
            break;
        }
        case NODE_IF: {
            IfNode *ifn = (IfNode*)node;
            sem_check_expr(ctx, ifn->condition);
            sem_check_block(ctx, ifn->then_body);
            if (ifn->else_body) sem_check_block(ctx, ifn->else_body);
            break;
        }
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode*)node;
            sem_check_expr(ctx, wn->condition);
            ctx->in_loop++;
            sem_check_block(ctx, wn->body);
            ctx->in_loop--;
            break;
        }
        case NODE_LOOP: {
            LoopNode *ln = (LoopNode*)node;
            sem_check_expr(ctx, ln->iterations);
            ctx->in_loop++;
            sem_check_block(ctx, ln->body);
            ctx->in_loop--;
            break;
        }
        case NODE_FOR_IN: {
            ForInNode *fn = (ForInNode*)node;
            sem_check_expr(ctx, fn->collection);
            ctx->in_loop++;
            sem_scope_enter(ctx, 0, (VarType){0});
            
            // Register iteration variable
            // TODO: Infer type from collection (Flux or Array)
            VarType iter_type = {TYPE_AUTO}; 
            sem_symbol_add(ctx, fn->var_name, SYM_VAR, iter_type);
            
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
            sem_check_expr(ctx, node); // Expression used as statement
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

void sem_check_func_def(SemanticCtx *ctx, FuncDefNode *node) {
    sem_scope_enter(ctx, 1, node->ret_type);
    
    // Add parameters to scope
    Parameter *p = node->params;
    while (p) {
        sem_symbol_add(ctx, p->name, SYM_VAR, p->type);
        p = p->next;
    }
    
    // Check body
    sem_check_block(ctx, node->body);
    
    sem_scope_exit(ctx);
}

void sem_check_node(SemanticCtx *ctx, ASTNode *node) {
    if (node->type == NODE_FUNC_DEF) sem_check_func_def(ctx, (FuncDefNode*)node);
    else if (node->type == NODE_CLASS) {
        ClassNode *cn = (ClassNode*)node;
        // Enter class scope again to check method bodies
        SemSymbol *sym = sem_symbol_lookup(ctx, cn->name);
        if (sym && sym->inner_scope) {
            SemScope *old = ctx->current_scope;
            ctx->current_scope = sym->inner_scope;
            
            ASTNode *mem = cn->members;
            while(mem) {
                if (mem->type == NODE_FUNC_DEF) sem_check_func_def(ctx, (FuncDefNode*)mem);
                // VarDecls are checked during scan, but initializers might need check?
                mem = mem->next;
            }
            
            ctx->current_scope = old;
        }
    }
    else if (node->type == NODE_NAMESPACE) {
        NamespaceNode *ns = (NamespaceNode*)node;
        SemSymbol *sym = sem_symbol_lookup(ctx, ns->name);
        if (sym && sym->inner_scope) {
            SemScope *old = ctx->current_scope;
            ctx->current_scope = sym->inner_scope;
            sem_check_block(ctx, ns->body);
            ctx->current_scope = old;
        }
    }
    else if (node->type == NODE_VAR_DECL) {
        sem_check_var_decl(ctx, (VarDeclNode*)node);
    }
    else {
        sem_check_stmt(ctx, node);
    }
}

int sem_check_program(SemanticCtx *ctx, ASTNode *root) {
    if (!root) return 0;
    
    // Pass 1: Gather Types/Functions
    sem_scan_top_level(ctx, root);
    
    if (ctx->error_count > 0) return ctx->error_count;
    
    // Pass 2: Verify Bodies
    ASTNode *curr = root;
    while (curr) {
        sem_check_node(ctx, curr);
        curr = curr->next;
    }
    
    return ctx->error_count;
}
