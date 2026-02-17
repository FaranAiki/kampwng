#include "semantic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h> // For uintptr_t

// --- SIDE TABLE LOGIC (Hash Map) ---

// Hash function for pointers (ASTNode addresses)
static unsigned int hash_ptr(ASTNode *node) {
    uintptr_t ptr_val = (uintptr_t)node;
    // Shift to ignore alignment (pointers are often 8-byte aligned)
    return (unsigned int)((ptr_val >> 3) % TYPE_TABLE_SIZE);
}

void sem_set_node_type(SemanticCtx *ctx, ASTNode *node, VarType type) {
    if (!node) return;
    unsigned int idx = hash_ptr(node);
    
    // Check for existing to overwrite
    TypeEntry *curr = ctx->type_buckets[idx];
    while (curr) {
        if (curr->node == node) {
            curr->type = type;
            return;
        }
        curr = curr->next;
    }
    
    // Insert new
    TypeEntry *entry = malloc(sizeof(TypeEntry));
    entry->node = node;
    entry->type = type;
    entry->next = ctx->type_buckets[idx];
    ctx->type_buckets[idx] = entry;
}

VarType sem_get_node_type(SemanticCtx *ctx, ASTNode *node) {
    if (!node) return (VarType){TYPE_UNKNOWN, 0, NULL, 0, 0};
    
    unsigned int idx = hash_ptr(node);
    TypeEntry *curr = ctx->type_buckets[idx];
    while (curr) {
        if (curr->node == node) return curr->type;
        curr = curr->next;
    }
    return (VarType){TYPE_UNKNOWN, 0, NULL, 0, 0};
}

// --- SYMBOL TABLE LOGIC (Scopes) ---

void sem_init(SemanticCtx *ctx) {
    ctx->global_scope = calloc(1, sizeof(SemScope));
    ctx->current_scope = ctx->global_scope;
    ctx->error_count = 0;
    ctx->in_loop = 0;
    ctx->in_switch = 0;
    
    // Initialize Side Table buckets
    for (int i = 0; i < TYPE_TABLE_SIZE; i++) {
        ctx->type_buckets[i] = NULL;
    }
}

void sem_cleanup(SemanticCtx *ctx) {
    // TODO: Free scopes and side table entries
    // For now, rely on OS cleanup or add recursive free logic here
}

void sem_scope_enter(SemanticCtx *ctx, int is_func, VarType ret_type) {
    SemScope *new_scope = malloc(sizeof(SemScope));
    new_scope->symbols = NULL;
    new_scope->parent = ctx->current_scope;
    new_scope->is_function_scope = is_func;
    new_scope->expected_ret_type = ret_type;
    
    ctx->current_scope = new_scope;
}

void sem_scope_exit(SemanticCtx *ctx) {
    if (ctx->current_scope->parent) {
        SemScope *old = ctx->current_scope;
        ctx->current_scope = old->parent;
        // Ideally free(old)
    }
}

SemSymbol* sem_symbol_add(SemanticCtx *ctx, const char *name, SymbolKind kind, VarType type) {
    SemSymbol *sym = malloc(sizeof(SemSymbol));
    sym->name = strdup(name);
    sym->kind = kind;
    sym->type = type;
    sym->param_types = NULL;
    sym->param_count = 0;
    sym->is_mutable = 1; 
    sym->inner_scope = NULL;
    
    sym->next = ctx->current_scope->symbols;
    ctx->current_scope->symbols = sym;
    return sym;
}

SemSymbol* sem_symbol_lookup(SemanticCtx *ctx, const char *name) {
    SemScope *scope = ctx->current_scope;
    while (scope) {
        SemSymbol *sym = scope->symbols;
        while (sym) {
            if (strcmp(sym->name, name) == 0) return sym;
            sym = sym->next;
        }
        scope = scope->parent;
    }
    return NULL;
}

// --- HELPER LOGIC ---

int sem_types_are_equal(VarType a, VarType b) {
    if (a.base != b.base) return 0;
    if (a.ptr_depth != b.ptr_depth) return 0;
    if (a.array_size != b.array_size) return 0;
    // Unsigned check can be loose depending on strictness preferences
    if (a.is_unsigned != b.is_unsigned) return 0; 
    
    if (a.base == TYPE_CLASS) {
        if (a.class_name && b.class_name) {
            return strcmp(a.class_name, b.class_name) == 0;
        }
        return 0;
    }
    return 1;
}

int sem_types_are_compatible(VarType dest, VarType src) {
    if (sem_types_are_equal(dest, src)) return 1;

    // Auto resolution (allowed target)
    if (dest.base == TYPE_AUTO) return 1; 

    // Numeric promotions
    int dest_is_num = (dest.base >= TYPE_INT && dest.base <= TYPE_LONG_DOUBLE);
    int src_is_num = (src.base >= TYPE_INT && src.base <= TYPE_LONG_DOUBLE);
    
    if (dest_is_num && src_is_num && dest.ptr_depth == 0 && src.ptr_depth == 0) {
        return 1; // Allow implicit numeric casts
    }
    
    // Array decay (int[10] -> int*)
    if (src.array_size > 0 && dest.ptr_depth == src.ptr_depth + 1 && dest.base == src.base) {
        return 1; 
    }

    // void* generic
    if (dest.base == TYPE_VOID && dest.ptr_depth > 0 && src.ptr_depth > 0) return 1;

    return 0;
}

char* sem_type_to_str(VarType t) {
    static char buf[128];
    const char *base = "unknown";
    switch(t.base) {
        case TYPE_INT: base = "int"; break;
        case TYPE_VOID: base = "void"; break;
        case TYPE_STRING: base = "string"; break;
        case TYPE_FLOAT: base = "float"; break;
        case TYPE_BOOL: base = "bool"; break;
        case TYPE_CLASS: base = t.class_name ? t.class_name : "class"; break;
        default: base = "type"; break;
    }
    snprintf(buf, 128, "%s%s", base, t.ptr_depth ? "*" : "");
    return buf;
}
