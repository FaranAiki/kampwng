#include "semantic.h"

void enter_scope(SemCtx *ctx) {
    Scope *s = malloc(sizeof(Scope));
    s->symbols = NULL;
    s->parent = ctx->current_scope;
    ctx->current_scope = s;
}

void exit_scope(SemCtx *ctx) {
    if (!ctx->current_scope) return;
    Scope *s = ctx->current_scope;
    
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

void add_symbol_semantic(SemCtx *ctx, const char *name, VarType type, int is_mut, int is_arr, int arr_size, int line, int col) {
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

SemSymbol* find_symbol_current_scope(SemCtx *ctx, const char *name) {
    SemSymbol *s = ctx->current_scope->symbols;
    while(s) {
        if (strcmp(s->name, name) == 0) return s;
        s = s->next;
    }
    return NULL;
}

SemSymbol* find_symbol_semantic(SemCtx *ctx, const char *name) {
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

void add_func(SemCtx *ctx, const char *name, char *mangled, VarType ret, VarType *params, int pcount, int is_flux) {
    SemFunc *f = malloc(sizeof(SemFunc));
    f->name = strdup(name);
    f->mangled_name = strdup(mangled);
    f->ret_type = ret;
    f->param_types = params; 
    f->param_count = pcount;
    f->is_flux = is_flux;
    f->next = ctx->functions;
    ctx->functions = f;
}

SemFunc* resolve_overload(SemCtx *ctx, ASTNode *call_node, const char *name, ASTNode *args_list) {
    SemFunc *best = NULL;
    int best_score = 99999;
    
    SemFunc *cand = ctx->functions;
    while(cand) {
        if (strcmp(cand->name, name) == 0) {
            int argc = 0;
            ASTNode *a = args_list;
            while(a) { argc++; a = a->next; }
            
            if (argc == cand->param_count) {
                int score = 0;
                int possible = 1;
                
                ASTNode *arg = args_list;
                for(int i=0; i<argc; i++) {
                    VarType arg_t = check_expr(ctx, arg); 
                    int cost = get_conversion_cost(arg_t, cand->param_types[i]);
                    if (cost == -1) { possible = 0; break; }
                    score += cost;
                    arg = arg->next;
                }
                
                if (possible) {
                    if (score < best_score) {
                        best_score = score;
                        best = cand;
                    }
                }
            }
        }
        cand = cand->next;
    }
    return best;
}

SemFunc* find_sem_func(SemCtx *ctx, const char *name) {
    SemFunc *f = ctx->functions;
    while(f) {
        if (strcmp(f->name, name) == 0) return f;
        f = f->next;
    }
    return NULL;
}

SemEnum* find_sem_enum(SemCtx *ctx, const char *name) {
    SemEnum *e = ctx->enums;
    while(e) {
        if (strcmp(e->name, name) == 0) return e;
        e = e->next;
    }
    return NULL;
}

void add_class(SemCtx *ctx, const char *name, const char *parent, char **traits, int trait_count, int is_union) {
    SemClass *c = malloc(sizeof(SemClass));
    c->name = strdup(name);
    c->parent_name = parent ? strdup(parent) : NULL;
    
    c->trait_count = trait_count;
    c->traits = NULL;
    if (trait_count > 0) {
        c->traits = malloc(sizeof(char*) * trait_count);
        for(int i=0; i<trait_count; i++) c->traits[i] = strdup(traits[i]);
    }
    
    c->members = NULL; 
    c->is_union = is_union;
    c->next = ctx->classes;
    ctx->classes = c;
}

SemClass* find_sem_class(SemCtx *ctx, const char *name) {
    SemClass *c = ctx->classes;
    while(c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next;
    }
    return NULL;
}

SemSymbol* find_member(SemCtx *ctx, const char *class_name, const char *member_name) {
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
    
    // Check traits (if they have fields, usually they don't, but for robustness)
    for(int i=0; i<cls->trait_count; i++) {
        SemSymbol *res = find_member(ctx, cls->traits[i], member_name);
        if (res) return res;
    }

    return NULL;
}

int class_has_trait(SemCtx *ctx, const char *class_name, const char *trait_name) {
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

// --- Recursive Method Resolution ---
// Searches Class -> Traits -> Parent for a method name that matches args
SemFunc* resolve_method_in_hierarchy(SemCtx *ctx, ASTNode *call_node, const char *class_name, const char *method_name, ASTNode *args, char **out_owner_class) {
    
    // 1. Check Class itself
    char qualified[512];
    snprintf(qualified, sizeof(qualified), "%s.%s", class_name, method_name);
    SemFunc *f = resolve_overload(ctx, call_node, qualified, args);
    if (f) {
        if (out_owner_class) *out_owner_class = strdup(class_name);
        return f;
    }
    
    SemClass *cls = find_sem_class(ctx, class_name);
    if (!cls) return NULL;

    // 2. Check Traits
    for(int i=0; i<cls->trait_count; i++) {
        SemFunc *tf = resolve_method_in_hierarchy(ctx, call_node, cls->traits[i], method_name, args, out_owner_class);
        if (tf) return tf; // Found in trait
    }

    // 3. Check Parent
    if (cls->parent_name) {
        SemFunc *pf = resolve_method_in_hierarchy(ctx, call_node, cls->parent_name, method_name, args, out_owner_class);
        if (pf) return pf; // Found in parent
    }

    return NULL;
}
