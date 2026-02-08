#include "semantic.h"
#include "../diagnostic/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// --- Symbol Table Structures ---

typedef struct SemSymbol {
    char *name;
    VarType type;
    int is_mutable;
    int is_array;
    int array_size; // 0 if unknown or dynamic
    // Location for error reporting
    int decl_line;
    int decl_col;
    struct SemSymbol *next;
} SemSymbol;

typedef struct SemFunc {
    char *name;
    VarType ret_type;
    struct SemFunc *next;
} SemFunc;

typedef struct SemEnum {
    char *name;
    struct SemEnumMember { char *name; struct SemEnumMember *next; } *members;
    struct SemEnum *next;
} SemEnum;

typedef struct SemClass {
    char *name;
    char *parent_name; 
    char **traits;      // List of implemented traits
    int trait_count;
    SemSymbol *members; // List of class fields
    struct SemClass *next;
} SemClass;

typedef struct Scope {
    SemSymbol *symbols;
    struct Scope *parent;
} Scope;

typedef struct {
    Scope *current_scope;
    SemFunc *functions;
    SemClass *classes;
    SemEnum *enums; // Added
    
    int error_count;
    
    // Context tracking
    VarType current_func_ret_type; // For checking return types
    int in_loop;                   // For checking break/continue
    const char *current_class;     // For 'this' context
    const char *source_code;       // For error reporting
} SemCtx;

// --- Helper Functions ---

static void sem_error(SemCtx *ctx, ASTNode *node, const char *fmt, ...) {
    ctx->error_count++;

    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Token t;
    t.line = node ? node->line : 0;
    t.col = node ? node->col : 0;
    t.text = NULL;
    
    Lexer l;
    if (ctx->source_code) {
        lexer_init(&l, ctx->source_code);
    }
    
    report_error(ctx->source_code ? &l : NULL, t, msg);
}

static void sem_reason(SemCtx *ctx, int line, int col, const char *fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Token t;
    t.line = line;
    t.col = col;
    t.text = NULL;
    
    Lexer l;
    if (ctx->source_code) {
        lexer_init(&l, ctx->source_code);
    }
    report_reason(ctx->source_code ? &l : NULL, t, msg);
}

static void sem_suggestion(SemCtx *ctx, ASTNode *node, const char *suggestion) {
    Token t;
    t.line = node ? node->line : 0;
    t.col = node ? node->col : 0;
    t.text = NULL;
    
    Lexer l;
    if (ctx->source_code) {
        lexer_init(&l, ctx->source_code);
    }
    report_suggestion(ctx->source_code ? &l : NULL, t, suggestion);
}

static int are_types_equal(VarType a, VarType b) {
    // Allow void* to be compatible with any pointer type (for malloc)
    if (a.ptr_depth > 0 && b.ptr_depth > 0) {
        if (a.base == TYPE_VOID || b.base == TYPE_VOID) return 1;
    }

    if (a.base != b.base) {
        if (a.base == TYPE_AUTO || b.base == TYPE_AUTO) return 1;
        
        // Implicit string compatibility
        if (a.base == TYPE_STRING && b.base == TYPE_CHAR && b.ptr_depth == 1) return 1;
        if (b.base == TYPE_STRING && a.base == TYPE_CHAR && a.ptr_depth == 1) return 1;

        return 0;
    }
    if (a.ptr_depth != b.ptr_depth) return 0;
    if (a.base == TYPE_CLASS) {
        if (a.class_name && b.class_name) {
            return strcmp(a.class_name, b.class_name) == 0;
        }
        return 0;
    }
    return 1;
}

static const char* type_to_str(VarType t) {
    static char buffers[4][128];
    static int idx = 0;
    char *buf = buffers[idx];
    idx = (idx + 1) % 4;

    const char *base;
    switch (t.base) {
        case TYPE_INT: base = "int"; break;
        case TYPE_CHAR: base = "char"; break;
        case TYPE_BOOL: base = "bool"; break;
        case TYPE_FLOAT: base = "single"; break;
        case TYPE_DOUBLE: base = "double"; break;
        case TYPE_VOID: base = "void"; break;
        case TYPE_STRING: base = "string"; break;
        case TYPE_CLASS: base = t.class_name ? t.class_name : "class"; break;
        case TYPE_UNKNOWN: base = "unknown"; break;
        case TYPE_AUTO: base = "auto"; break;
        default: base = "???"; break;
    }
    strcpy(buf, base);
    for(int i=0; i<t.ptr_depth; i++) strcat(buf, "*");
    if (t.array_size > 0) {
        char tmp[16]; sprintf(tmp, "[%d]", t.array_size);
        strcat(buf, tmp);
    }
    return buf;
}

static const char* find_closest_type_name(SemCtx *ctx, const char *name) {
    const char *primitives[] = {
        "int", "char", "bool", "single", "double", "void", "string", "let", "auto", NULL
    };
    
    const char *best = NULL;
    int min_dist = 3; 

    for (int i = 0; primitives[i]; i++) {
        int d = levenshtein_dist(name, primitives[i]);
        if (d < min_dist) {
            min_dist = d;
            best = primitives[i];
        }
    }

    SemClass *c = ctx->classes;
    while(c) {
        int d = levenshtein_dist(name, c->name);
        if (d < min_dist) {
            min_dist = d;
            best = c->name;
        }
        c = c->next;
    }
    
    return best;
}

static const char* find_closest_func_name(SemCtx *ctx, const char *name) {
    const char *builtins[] = {"print", "printf", "input", "malloc", "alloc", "free", "setjmp", "longjmp", NULL};
    const char *best = NULL;
    int min_dist = 3;

    for (int i = 0; builtins[i]; i++) {
        int d = levenshtein_dist(name, builtins[i]);
        if (d < min_dist) {
            min_dist = d;
            best = builtins[i];
        }
    }

    SemFunc *f = ctx->functions;
    while(f) {
        int d = levenshtein_dist(name, f->name);
        if (d < min_dist) {
            min_dist = d;
            best = f->name;
        }
        f = f->next;
    }
    return best;
}

static const char* find_closest_var_name(SemCtx *ctx, const char *name) {
    const char *best = NULL;
    int min_dist = 3;
    
    Scope *scope = ctx->current_scope;
    while(scope) {
        SemSymbol *s = scope->symbols;
        while(s) {
            int d = levenshtein_dist(name, s->name);
            if (d < min_dist) {
                min_dist = d;
                best = s->name;
            }
            s = s->next;
        }
        scope = scope->parent;
    }
    return best;
}

// --- Scope Management ---

static void enter_scope(SemCtx *ctx) {
    Scope *s = malloc(sizeof(Scope));
    s->symbols = NULL;
    s->parent = ctx->current_scope;
    ctx->current_scope = s;
}

static void exit_scope(SemCtx *ctx) {
    if (!ctx->current_scope) return;
    Scope *s = ctx->current_scope;
    
    // Free symbols
    SemSymbol *sym = s->symbols;
    while(sym) {
        SemSymbol *next = sym->next;
        free(sym->name);
        free(sym);
        sym = next;
    }
    
    ctx->current_scope = s->parent;
    free(s);
}

static void add_symbol(SemCtx *ctx, const char *name, VarType type, int is_mut, int is_arr, int arr_size, int line, int col) {
    SemSymbol *s = malloc(sizeof(SemSymbol));
    s->name = strdup(name);
    s->type = type;
    s->is_mutable = is_mut;
    s->is_array = is_arr;
    s->array_size = arr_size;
    s->decl_line = line;
    s->decl_col = col;
    s->next = ctx->current_scope->symbols;
    ctx->current_scope->symbols = s;
}

static SemSymbol* find_symbol_current_scope(SemCtx *ctx, const char *name) {
    SemSymbol *s = ctx->current_scope->symbols;
    while(s) {
        if (strcmp(s->name, name) == 0) return s;
        s = s->next;
    }
    return NULL;
}

static SemSymbol* find_symbol(SemCtx *ctx, const char *name) {
    Scope *scope = ctx->current_scope;
    while(scope) {
        SemSymbol *s = scope->symbols;
        while(s) {
            if (strcmp(s->name, name) == 0) return s;
            s = s->next;
        }
        scope = scope->parent;
    }
    return NULL;
}

static void add_func(SemCtx *ctx, const char *name, VarType ret) {
    SemFunc *f = malloc(sizeof(SemFunc));
    f->name = strdup(name);
    f->ret_type = ret;
    f->next = ctx->functions;
    ctx->functions = f;
}

static SemFunc* find_func(SemCtx *ctx, const char *name) {
    SemFunc *f = ctx->functions;
    while(f) {
        if (strcmp(f->name, name) == 0) return f;
        f = f->next;
    }
    return NULL;
}

static SemEnum* find_sem_enum(SemCtx *ctx, const char *name) {
    SemEnum *e = ctx->enums;
    while(e) {
        if (strcmp(e->name, name) == 0) return e;
        e = e->next;
    }
    return NULL;
}

static void add_class(SemCtx *ctx, const char *name, const char *parent, char **traits, int trait_count) {
    SemClass *c = malloc(sizeof(SemClass));
    c->name = strdup(name);
    c->parent_name = parent ? strdup(parent) : NULL;
    
    c->trait_count = trait_count;
    c->traits = NULL;
    if (trait_count > 0) {
        c->traits = malloc(sizeof(char*) * trait_count);
        for(int i=0; i<trait_count; i++) c->traits[i] = strdup(traits[i]);
    }
    
    c->members = NULL; // Init members
    c->next = ctx->classes;
    ctx->classes = c;
}

static SemClass* find_sem_class(SemCtx *ctx, const char *name) {
    SemClass *c = ctx->classes;
    while(c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next;
    }
    return NULL;
}

static SemSymbol* find_member(SemCtx *ctx, const char *class_name, const char *member_name) {
    SemClass *cls = find_sem_class(ctx, class_name);
    if (!cls) return NULL;

    SemSymbol *mem = cls->members;
    while(mem) {
        if (strcmp(mem->name, member_name) == 0) return mem;
        mem = mem->next;
    }

    if (cls->parent_name) {
        return find_member(ctx, cls->parent_name, member_name);
    }
    return NULL;
}

static int class_has_trait(SemCtx *ctx, const char *class_name, const char *trait_name) {
    SemClass *c = find_sem_class(ctx, class_name);
    if (!c) return 0;
    
    for (int i=0; i<c->trait_count; i++) {
        if (strcmp(c->traits[i], trait_name) == 0) return 1;
    }
    
    if (c->parent_name) {
        return class_has_trait(ctx, c->parent_name, trait_name);
    }
    return 0;
}

// --- Traversal Prototypes ---

static VarType check_expr(SemCtx *ctx, ASTNode *node);
static void check_stmt(SemCtx *ctx, ASTNode *node);

// --- Checks ---

static VarType check_expr(SemCtx *ctx, ASTNode *node) {
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
            SemSymbol *sym = find_symbol(ctx, name);
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

                // Check for Enum Name used as reference (e.g. implicit to namespace)
                // Although EnumName alone is not a value.
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

        case NODE_ASSIGN: {
            AssignNode *a = (AssignNode*)node;
            VarType l_type = unknown;
            int is_const = 0;
            
            if (a->name) {
                SemSymbol *sym = find_symbol(ctx, a->name);
                if (!sym) {
                    // Check Implicit 'this' member assign
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
                    int compatible = 0;
                    if (l_type.ptr_depth > 0 && r_type.array_size > 0 && l_type.base == r_type.base) compatible = 1;
                    
                    if (!compatible) {
                         sem_error(ctx, node, "Type mismatch in assignment. Expected '%s', got '%s'", type_to_str(l_type), type_to_str(r_type));
                    }
                }
            }
            return l_type;
        }

        case NODE_CALL: {
            CallNode *c = (CallNode*)node;
            if (strcmp(c->name, "print") == 0 || strcmp(c->name, "printf") == 0) {
                 VarType ret = {TYPE_VOID, 0, NULL};
                 return ret;
            }
            
            if (strcmp(c->name, "input") == 0) {
                 VarType ret = {TYPE_STRING, 0, NULL};
                 return ret;
            }

            // Built-in memory management
            if (strcmp(c->name, "malloc") == 0 || strcmp(c->name, "alloc") == 0) {
                 if (!c->args) {
                     sem_error(ctx, node, "'%s' requires 1 argument (size)", c->name);
                 } else if (c->args->next) {
                     sem_error(ctx, node, "'%s' requires exactly 1 argument", c->name);
                 } else {
                     VarType t = check_expr(ctx, c->args);
                     if (t.base != TYPE_INT) {
                         sem_error(ctx, c->args, "Size argument must be an integer");
                     }
                 }
                 // Return void* (TYPE_VOID with pointer depth 1)
                 VarType ret = {TYPE_VOID, 1, NULL};
                 return ret;
            }

            if (strcmp(c->name, "free") == 0) {
                 if (!c->args) {
                     sem_error(ctx, node, "'free' requires 1 argument");
                 } else if (c->args->next) {
                     sem_error(ctx, node, "'free' requires exactly 1 argument");
                 } else {
                     VarType t = check_expr(ctx, c->args);
                     // Expect a pointer or array or string (if dynamic)
                     if (t.ptr_depth == 0 && t.array_size == 0 && t.base != TYPE_STRING) {
                          sem_error(ctx, c->args, "Expected pointer argument for 'free'");
                     }
                 }
                 VarType ret = {TYPE_VOID, 0, NULL};
                 return ret;
            }
            
            // setjmp: int setjmp(void* buffer)
            if (strcmp(c->name, "setjmp") == 0) {
                if (!c->args) {
                     sem_error(ctx, node, "'setjmp' requires 1 argument (jmp_buf buffer)");
                } else if (c->args->next) {
                     sem_error(ctx, node, "'setjmp' requires exactly 1 argument");
                }
                // Argument can be array or pointer
                VarType ret = {TYPE_INT, 0, NULL};
                return ret;
            }
            
            // longjmp: void longjmp(void* buffer, int val)
            if (strcmp(c->name, "longjmp") == 0) {
                if (!c->args || !c->args->next) {
                     sem_error(ctx, node, "'longjmp' requires 2 arguments (jmp_buf buffer, int val)");
                } else if (c->args->next->next) {
                     sem_error(ctx, node, "'longjmp' requires exactly 2 arguments");
                } else {
                     VarType t2 = check_expr(ctx, c->args->next);
                     if (t2.base != TYPE_INT) sem_error(ctx, c->args->next, "Second argument to longjmp must be integer");
                }
                VarType ret = {TYPE_VOID, 0, NULL};
                return ret;
            }

            SemFunc *f = find_func(ctx, c->name);
            if (!f) {
                SemClass *cls = ctx->classes;
                int is_cls = 0;
                while(cls) { if(strcmp(cls->name, c->name) == 0) { is_cls=1; break; } cls = cls->next; }
                
                if (is_cls) {
                    VarType ret = {TYPE_CLASS, 0, strdup(c->name)};
                    // Construct args check? For now simplified.
                    return ret;
                }

                sem_error(ctx, node, "Undefined symbol '%s'", c->name);

                const char *type_guess = find_closest_type_name(ctx, c->name);
                const char *func_guess = find_closest_func_name(ctx, c->name);
                
                if (type_guess) {
                    Token t; t.line = node->line; t.col = node->col; t.text = NULL;
                    Lexer l; lexer_init(&l, ctx->source_code);
                    char hint_msg[256];
                    snprintf(hint_msg, sizeof(hint_msg), "'%s' looks like type '%s'. Did you mean to declare a variable?", c->name, type_guess);
                    report_hint(&l, t, hint_msg);
                } else if (func_guess) {
                    sem_suggestion(ctx, node, func_guess);
                }

                return unknown;
            }
            
            ASTNode *arg = c->args;
            while(arg) {
                check_expr(ctx, arg);
                arg = arg->next;
            }
            
            return f->ret_type;
        }
        
        case NODE_METHOD_CALL: {
            MethodCallNode *mc = (MethodCallNode*)node;
            
            if (mc->object->type == NODE_VAR_REF) {
                char *ns_name = ((VarRefNode*)mc->object)->name;
                char qname[512];
                snprintf(qname, sizeof(qname), "%s.%s", ns_name, mc->method_name);
                
                SemFunc *f = find_func(ctx, qname);
                if (f) {
                     ASTNode *arg = mc->args;
                     while(arg) { check_expr(ctx, arg); arg = arg->next; }
                     return f->ret_type;
                }
            }
            
            VarType obj_type = check_expr(ctx, mc->object);
            if (obj_type.base == TYPE_CLASS && obj_type.class_name) {
                const char *curr_cls_name = obj_type.class_name;
                while (curr_cls_name) {
                    char qname[512];
                    snprintf(qname, sizeof(qname), "%s.%s", curr_cls_name, mc->method_name);
                    SemFunc *f = find_func(ctx, qname);
                    if (f) {
                         ASTNode *arg = mc->args;
                         while(arg) { check_expr(ctx, arg); arg = arg->next; }
                         return f->ret_type;
                    }
                    
                    SemClass *cls = find_sem_class(ctx, curr_cls_name);
                    
                    if (cls && cls->parent_name) {
                        curr_cls_name = cls->parent_name;
                    } else {
                        break; 
                    }
                }
            }
            
            ASTNode *arg = mc->args;
            while(arg) { check_expr(ctx, arg); arg = arg->next; }
            
            ASTNode *err_node = node;
            if ((node->line == 0 && node->col == 0) && mc->object) {
                err_node = mc->object;
            }
            sem_error(ctx, err_node, "Method '%s' not found", mc->method_name);

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
            
            // Result is the trait type (as a class)
            // Preserves pointer depth of the original object
            VarType trait_type = {TYPE_CLASS, obj_t.ptr_depth, strdup(ta->trait_name)};
            trait_type.array_size = obj_t.array_size;
            return trait_type;
        }

        case NODE_ARRAY_ACCESS: {
            ArrayAccessNode *aa = (ArrayAccessNode*)node;
            
            // Enum String Mapping Support: Enum[val]
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
            
            if (aa->index->type == NODE_LITERAL) {
                int idx = ((LiteralNode*)aa->index)->val.int_val;
                if (aa->target->type == NODE_VAR_REF) {
                    SemSymbol *sym = find_symbol(ctx, ((VarRefNode*)aa->target)->name);
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
             
             // Enum Member Access Support: Enum.Member
             if (ma->object->type == NODE_VAR_REF) {
                 char *name = ((VarRefNode*)ma->object)->name;
                 SemEnum *se = find_sem_enum(ctx, name);
                 if (se) {
                     // Verify member existence
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

        default:
            return unknown;
    }
}

static void check_stmt(SemCtx *ctx, ASTNode *node) {
    if (!node) return;
    
    switch(node->type) {
        case NODE_VAR_DECL: {
            VarDeclNode *vd = (VarDeclNode*)node;
            SemSymbol *existing = find_symbol_current_scope(ctx, vd->name);
            if (existing) {
                sem_error(ctx, node, "Redefinition of variable '%s' in current scope", vd->name);
                if (existing->decl_line > 0) {
                    sem_reason(ctx, existing->decl_line, existing->decl_col, "Previous definition of '%s' was here", vd->name);
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
                     int ok = 0;
                     if (vd->var_type.base == TYPE_STRING && init_t.base == TYPE_STRING) ok = 1;
                     if (vd->var_type.base == TYPE_CHAR && vd->is_array && init_t.base == TYPE_STRING) ok = 1;
                     if (vd->var_type.base == TYPE_CHAR && vd->var_type.ptr_depth == 1 && init_t.base == TYPE_STRING) ok = 1;
                     
                     // Allow Enum (Int) assigned to Int
                     if (vd->var_type.base == TYPE_INT && init_t.base == TYPE_INT) ok = 1;

                     if (!ok) {
                        sem_error(ctx, node, "Variable '%s' type mismatch. Declared '%s', init '%s'", 
                                  vd->name, type_to_str(vd->var_type), type_to_str(init_t));
                        
                        if (vd->var_type.base == TYPE_CHAR && init_t.base == TYPE_STRING) {
                             Token t; t.line = node->line; t.col = node->col; t.text = NULL;
                             Lexer l; lexer_init(&l, ctx->source_code);
                             report_hint(&l, t, "Possible typedef mismatch. Note that 'string' literals are compatible with 'char*' or 'char[]'.");
                        }
                     }
                } else {
                    if (vd->is_array) {
                        if (init_t.array_size <= 0 && init_t.ptr_depth == 0) {
                             sem_error(ctx, node, "Cannot initialize array '%s' with scalar type '%s'", 
                                      vd->name, type_to_str(init_t));
                        }
                    } else {
                        if (init_t.array_size > 0) {
                             sem_error(ctx, node, "Cannot initialize scalar '%s' with array type '%s'", 
                                      vd->name, type_to_str(init_t));
                        }
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

            add_symbol(ctx, vd->name, inferred, vd->is_mutable, vd->is_array, arr_size, node->line, node->col);
            break;
        }

        case NODE_RETURN: {
            ReturnNode *r = (ReturnNode*)node;
            VarType ret_t = {TYPE_VOID, 0, NULL};
            if (r->value) ret_t = check_expr(ctx, r->value);
            
            if (!are_types_equal(ctx->current_func_ret_type, ret_t)) {
                sem_error(ctx, node, "Return type mismatch. Expected '%s', got '%s'", 
                          type_to_str(ctx->current_func_ret_type), type_to_str(ret_t));
            }
            break;
        }

        case NODE_IF: {
            IfNode *i = (IfNode*)node;
            check_expr(ctx, i->condition);
            enter_scope(ctx);
            check_stmt(ctx, i->then_body);
            exit_scope(ctx);
            if (i->else_body) {
                enter_scope(ctx);
                check_stmt(ctx, i->else_body);
                exit_scope(ctx);
            }
            break;
        }

        case NODE_SWITCH: {
            SwitchNode *s = (SwitchNode*)node;
            VarType ct = check_expr(ctx, s->condition);
            // Basic check: must be scalar
            if (ct.base != TYPE_INT && ct.base != TYPE_CHAR) {
                // Warning or error for non-integer switch?
            }
            
            enter_scope(ctx);
            
            int prev_loop = ctx->in_loop;
            ctx->in_loop = 1; // Allow break inside switch
            
            ASTNode *c = s->cases;
            while(c) {
                CaseNode *cn = (CaseNode*)c;
                VarType vt = check_expr(ctx, cn->value);
                if (!are_types_equal(ct, vt)) {
                    // Type mismatch warning
                }
                check_stmt(ctx, cn->body);
                c = c->next;
            }
            if (s->default_case) check_stmt(ctx, s->default_case);
            
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
            check_stmt(ctx, l->body);
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
            check_stmt(ctx, w->body);
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
    
    if (node->next) check_stmt(ctx, node->next);
}

// --- Driver Logic ---

static void scan_declarations(SemCtx *ctx, ASTNode *node, const char *prefix) {
    while(node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            char *name = fd->name;
            char *qualified = NULL;
            if (prefix) {
                int len = strlen(prefix) + strlen(name) + 2;
                qualified = malloc(len);
                snprintf(qualified, len, "%s.%s", prefix, name);
                name = qualified;
            }
            add_func(ctx, name, fd->ret_type);
            if (qualified) free(qualified);
        } 
        else if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            char *name = cn->name;
            char *qualified = NULL;
            if (prefix) {
                int len = strlen(prefix) + strlen(name) + 2;
                qualified = malloc(len);
                snprintf(qualified, len, "%s.%s", prefix, name);
                name = qualified;
            }
            
            add_class(ctx, name, cn->parent_name, cn->traits.names, cn->traits.count);
            
            // Register Fields
            SemClass *cls = find_sem_class(ctx, name);
            if (cls) {
                ASTNode *mem = cn->members;
                while(mem) {
                    if (mem->type == NODE_VAR_DECL) {
                        VarDeclNode *vd = (VarDeclNode*)mem;
                        SemSymbol *s = malloc(sizeof(SemSymbol));
                        s->name = strdup(vd->name);
                        s->type = vd->var_type;
                        s->is_mutable = vd->is_mutable;
                        s->is_array = vd->is_array;
                        
                        int arr_size = 0;
                        if (vd->is_array) {
                            if (vd->array_size && vd->array_size->type == NODE_LITERAL) {
                                arr_size = ((LiteralNode*)vd->array_size)->val.int_val;
                            }
                        }
                        s->array_size = arr_size;
                        
                        s->decl_line = vd->base.line;
                        s->decl_col = vd->base.col;
                        s->next = cls->members;
                        cls->members = s;
                    }
                    mem = mem->next;
                }
            }

            // Recurse into class members so methods are registered as Class.Method
            scan_declarations(ctx, cn->members, name);

            if (qualified) free(qualified);
        }
        else if (node->type == NODE_NAMESPACE) {
             NamespaceNode *ns = (NamespaceNode*)node;
             char *new_prefix = ns->name;
             char *qualified = NULL;
             if (prefix) {
                 int len = strlen(prefix) + strlen(ns->name) + 2;
                 qualified = malloc(len);
                 snprintf(qualified, len, "%s.%s", prefix, ns->name);
                 new_prefix = qualified;
             }
             scan_declarations(ctx, ns->body, new_prefix);
             if (qualified) free(qualified);
        }
        else if (node->type == NODE_ENUM) {
            EnumNode *en = (EnumNode*)node;
            
            char *name = en->name;
            char *qualified = NULL;
            if (prefix) {
                 int len = strlen(prefix) + strlen(name) + 2;
                 qualified = malloc(len);
                 snprintf(qualified, len, "%s.%s", prefix, name);
                 name = qualified;
            }

            SemEnum *se = malloc(sizeof(SemEnum));
            se->name = strdup(name);
            se->members = NULL;
            se->next = ctx->enums;
            ctx->enums = se;

            EnumEntry *ent = en->entries;
            struct SemEnumMember **tail = &se->members;
            
            while(ent) {
                // Add to internal list
                struct SemEnumMember *m = malloc(sizeof(struct SemEnumMember));
                m->name = strdup(ent->name);
                m->next = NULL;
                *tail = m;
                tail = &m->next;

                // Add as Global Symbol (to support direct access)
                char *member_name = ent->name;
                char *qualified_mem = NULL;
                if (prefix) {
                     int len = strlen(prefix) + strlen(member_name) + 2;
                     qualified_mem = malloc(len);
                     snprintf(qualified_mem, len, "%s.%s", prefix, member_name);
                     member_name = qualified_mem;
                }
                
                // Note: We use "." for semantic scoping, but codegen uses "_" or expects mangled name.
                // However, semantic analysis just needs to know the symbol exists.
                // For direct access `let x = Ayam`, we register `Ayam`.
                // If inside namespace, `let x = NS.Ayam`? or `let x = Ayam` if inside NS?
                // The user example `let sarapan = Ayam` implies simple scope injection.
                
                VarType vt = {TYPE_INT, 0, NULL};
                
                // Register simple name (unqualified) if we are at top level or if we want direct injection
                // We'll register the simple name for now to satisfy "let x = Ayam".
                add_symbol(ctx, ent->name, vt, 0, 0, 0, en->base.line, en->base.col);
                
                if (qualified_mem) free(qualified_mem);
                ent = ent->next;
            }
            if (qualified) free(qualified);
        }

        node = node->next;
    }
}

static void check_program(SemCtx *ctx, ASTNode *node) {
    while(node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            ctx->current_func_ret_type = fd->ret_type;
            enter_scope(ctx);
            
            Parameter *p = fd->params;
            while(p) {
                add_symbol(ctx, p->name, p->type, 1, 0, 0, 0, 0); 
                p = p->next;
            }
            
            if (fd->class_name) {
                 ctx->current_class = fd->class_name;
            }

            check_stmt(ctx, fd->body);
            
            ctx->current_class = NULL;
            exit_scope(ctx);
        }
        else if (node->type == NODE_VAR_DECL) {
             check_stmt(ctx, node); 
        }
        else if (node->type == NODE_NAMESPACE) {
             check_program(ctx, ((NamespaceNode*)node)->body);
        }
        else if (node->type == NODE_CLASS) {
             // Basic class check
        }
        else {
            check_stmt(ctx, node);
        }
        node = node->next;
    }
}

int semantic_analysis(ASTNode *root, const char *source) {
    SemCtx ctx;
    ctx.current_scope = NULL;
    ctx.functions = NULL;
    ctx.classes = NULL;
    ctx.enums = NULL; // Init enums
    ctx.error_count = 0;
    ctx.in_loop = 0;
    ctx.current_class = NULL;
    ctx.source_code = source;
    
    enter_scope(&ctx);
    
    scan_declarations(&ctx, root, NULL);
    check_program(&ctx, root);
    
    exit_scope(&ctx);
    
    while(ctx.functions) { SemFunc *n = ctx.functions->next; free(ctx.functions->name); free(ctx.functions); ctx.functions = n; }
    while(ctx.classes) { 
        SemClass *n = ctx.classes->next; 
        free(ctx.classes->name); 
        if (ctx.classes->parent_name) free(ctx.classes->parent_name);
        
        if (ctx.classes->traits) {
            for(int i=0; i<ctx.classes->trait_count; i++) free(ctx.classes->traits[i]);
            free(ctx.classes->traits);
        }
        
        // Free members
        SemSymbol *mem = ctx.classes->members;
        while(mem) { SemSymbol *mn = mem->next; free(mem->name); free(mem); mem = mn; }
        free(ctx.classes); 
        ctx.classes = n; 
    }
    while(ctx.enums) {
        SemEnum *n = ctx.enums->next;
        struct SemEnumMember *m = ctx.enums->members;
        while(m) { struct SemEnumMember *next = m->next; free(m->name); free(m); m = next; }
        free(ctx.enums->name); free(ctx.enums);
        ctx.enums = n;
    }

    return ctx.error_count;
}
