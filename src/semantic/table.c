#include "semantic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h> 

static unsigned int hash_ptr(ASTNode *node) {
    uintptr_t ptr_val = (uintptr_t)node;
    return (unsigned int)((ptr_val >> 3) % TYPE_TABLE_SIZE);
}

void sem_set_node_type(SemanticCtx *ctx, ASTNode *node, VarType type) {
    if (!node) return;
    unsigned int idx = hash_ptr(node);
    
    TypeEntry *curr = ctx->type_buckets[idx];
    while (curr) {
        if (curr->node == node) {
            curr->type = type;
            return;
        }
        curr = curr->next;
    }
    
    if (!ctx->compiler_ctx || !ctx->compiler_ctx->arena) return;

    TypeEntry *entry = arena_alloc_type(ctx->compiler_ctx->arena, TypeEntry);
    entry->node = node;
    entry->type = type;
    entry->is_tainted = 0; 
    entry->is_impure = 0; 
    entry->next = ctx->type_buckets[idx];
    ctx->type_buckets[idx] = entry;
}

VarType sem_get_node_type(SemanticCtx *ctx, ASTNode *node) {
    if (!node) return (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
    
    unsigned int idx = hash_ptr(node);
    TypeEntry *curr = ctx->type_buckets[idx];
    while (curr) {
        if (curr->node == node) return curr->type;
        curr = curr->next;
    }
    return (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0};
}

void sem_set_node_tainted(SemanticCtx *ctx, ASTNode *node, int is_tainted) {
    if (!node) return;
    unsigned int idx = hash_ptr(node);
    TypeEntry *curr = ctx->type_buckets[idx];
    while (curr) {
        if (curr->node == node) {
            curr->is_tainted = is_tainted;
            return;
        }
        curr = curr->next;
    }
    
    sem_set_node_type(ctx, node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    ctx->type_buckets[idx]->is_tainted = is_tainted;
}

int sem_get_node_tainted(SemanticCtx *ctx, ASTNode *node) {
    if (!node) return 0;
    unsigned int idx = hash_ptr(node);
    TypeEntry *curr = ctx->type_buckets[idx];
    while (curr) {
        if (curr->node == node) return curr->is_tainted;
        curr = curr->next;
    }
    return 0; 
}

void sem_set_node_impure(SemanticCtx *ctx, ASTNode *node, int is_impure) {
    if (!node) return;
    unsigned int idx = hash_ptr(node);
    TypeEntry *curr = ctx->type_buckets[idx];
    while (curr) {
        if (curr->node == node) {
            curr->is_impure = is_impure;
            return;
        }
        curr = curr->next;
    }
    
    sem_set_node_type(ctx, node, (VarType){TYPE_UNKNOWN, 0, 0, NULL, 0, NULL, NULL, 0, 0, 0, 0});
    ctx->type_buckets[idx]->is_impure = is_impure;
}

int sem_get_node_impure(SemanticCtx *ctx, ASTNode *node) {
    if (!node) return 0;
    unsigned int idx = hash_ptr(node);
    TypeEntry *curr = ctx->type_buckets[idx];
    while (curr) {
        if (curr->node == node) return curr->is_impure;
        curr = curr->next;
    }
    return 0; 
}

static SemSymbol* find_in_scope_direct(SemScope *scope, const char *name) {
    SemSymbol *sym = scope->symbols;
    while (sym) {
        if (strcmp(sym->name, name) == 0) return sym;
        sym = sym->next;
    }
    return NULL;
}

void sem_init(SemanticCtx *ctx, CompilerContext *compiler_ctx) {
    ctx->compiler_ctx = compiler_ctx;
    
    if (compiler_ctx && compiler_ctx->arena) {
        ctx->global_scope = arena_alloc_type(compiler_ctx->arena, SemScope);
        memset(ctx->global_scope, 0, sizeof(SemScope));
    }
    
    ctx->current_scope = ctx->global_scope;
    ctx->current_func_sym = NULL;
    ctx->in_wash_block = 0;
    ctx->in_loop = 0;
    ctx->in_switch = 0;
    ctx->current_source = NULL;
    ctx->current_filename = NULL;
    
    for (int i = 0; i < TYPE_TABLE_SIZE; i++) {
        ctx->type_buckets[i] = NULL;
    }
}

void sem_cleanup(SemanticCtx *ctx) {
    ctx->current_scope = NULL;
    ctx->global_scope = NULL;
    ctx->current_func_sym = NULL;
    for (int i = 0; i < TYPE_TABLE_SIZE; i++) {
        ctx->type_buckets[i] = NULL;
    }
}

void sem_scope_enter(SemanticCtx *ctx, int is_func, VarType ret_type) {
    if (!ctx->compiler_ctx || !ctx->compiler_ctx->arena) return;

    SemScope *new_scope = arena_alloc_type(ctx->compiler_ctx->arena, SemScope);
    memset(new_scope, 0, sizeof(SemScope));

    new_scope->symbols = NULL;
    new_scope->parent = ctx->current_scope;
    new_scope->is_function_scope = is_func;
    new_scope->is_class_scope = 0; 
    new_scope->expected_ret_type = ret_type;
    
    ctx->current_scope = new_scope;
}

void sem_scope_exit(SemanticCtx *ctx) {
    if (ctx->current_scope->parent) {
        ctx->current_scope = ctx->current_scope->parent;
    }
}

SemSymbol* sem_symbol_add(SemanticCtx *ctx, const char *name, SymbolKind kind, VarType type) {
    if (!ctx->compiler_ctx || !ctx->compiler_ctx->arena) return NULL;

    SemSymbol *sym = arena_alloc_type(ctx->compiler_ctx->arena, SemSymbol);
    memset(sym, 0, sizeof(SemSymbol));

    sym->name = arena_strdup(ctx->compiler_ctx->arena, name);
    sym->kind = kind;
    sym->type = type;
    sym->param_types = NULL;
    sym->param_count = 0;
    sym->parent_name = NULL; 
    sym->is_mutable = 1; 
    sym->is_initialized = 1; 
    sym->is_pure = 1;      
    sym->is_pristine = 1;  
    sym->inner_scope = NULL;
    
    sym->next = ctx->current_scope->symbols;
    ctx->current_scope->symbols = sym;
    return sym;
}

SemSymbol* sem_symbol_lookup(SemanticCtx *ctx, const char *name, SemScope **out_scope) {
    SemScope *scope = ctx->current_scope;
    while (scope) {

        SemSymbol *sym = find_in_scope_direct(scope, name);
        if (sym) {
            if (out_scope) *out_scope = scope;
            return sym;
        }
        
        sym = scope->symbols;
        while (sym) {
            if (sym->kind == SYM_ENUM && sym->inner_scope) {
                SemSymbol *mem = sym->inner_scope->symbols;
                while (mem) {
                    if (strcmp(mem->name, name) == 0) {
                        if (out_scope) *out_scope = sym->inner_scope;
                        return mem;
                    }
                    mem = mem->next;
                }
            }
            sym = sym->next;
        }

        if (scope->is_class_scope && scope->class_sym && scope->class_sym->parent_name) {
            SemScope *search_scope = scope->parent; 
            SemSymbol *parent_class = NULL;
            
            while (search_scope) {
                parent_class = find_in_scope_direct(search_scope, scope->class_sym->parent_name);
                if (parent_class && parent_class->kind == SYM_CLASS) break;
                search_scope = search_scope->parent;
                parent_class = NULL;
            }

            if (parent_class && parent_class->inner_scope) {
                SemSymbol *inherited = find_in_scope_direct(parent_class->inner_scope, name);
                if (inherited) {
                    if (out_scope) *out_scope = parent_class->inner_scope;
                    return inherited;
                }
                
                SemSymbol *curr_cls = parent_class;
                while (curr_cls && curr_cls->parent_name) {
                    SemScope *gp_search = ctx->global_scope; 
                    SemSymbol *grandparent = find_in_scope_direct(gp_search, curr_cls->parent_name);
                    
                    if (grandparent && grandparent->kind == SYM_CLASS && grandparent->inner_scope) {
                         inherited = find_in_scope_direct(grandparent->inner_scope, name);
                         if (inherited) {
                             if (out_scope) *out_scope = grandparent->inner_scope;
                             return inherited;
                         }
                         curr_cls = grandparent;
                    } else {
                        break;
                    }
                }
            }
        }

        scope = scope->parent;
    }
    if (out_scope) *out_scope = NULL;
    return NULL;
}

int sem_types_are_equal(VarType a, VarType b) {
    if (a.base != b.base) return 0;
    if (a.ptr_depth != b.ptr_depth) return 0;
    if (a.vector_depth != b.vector_depth) return 0;
    if (a.array_size != b.array_size) return 0;
    if (a.is_unsigned != b.is_unsigned) return 0; 
    
    if (a.base == TYPE_CLASS || a.base == TYPE_ENUM) {
        if (a.class_name && b.class_name) {
            return strcmp(a.class_name, b.class_name) == 0;
        }
        return 0;
    }
    return 1;
}

int sem_types_are_compatible(VarType dest, VarType src) {
    if (sem_types_are_equal(dest, src)) return 1;

    if (dest.base == TYPE_AUTO) return 1; 
    
    if (dest.base == TYPE_STRING && src.base == TYPE_CHAR) return 1;

    int dest_is_num = (dest.base >= TYPE_INT && dest.base <= TYPE_LONG_DOUBLE);
    int src_is_num = (src.base >= TYPE_INT && src.base <= TYPE_LONG_DOUBLE);

    if (src.base == TYPE_ENUM && dest_is_num) return 1;
    if (dest.base == TYPE_ENUM && src_is_num) return 1;
    
    if (dest_is_num && src_is_num && dest.ptr_depth == 0 && src.ptr_depth == 0 && dest.vector_depth == 0 && src.vector_depth == 0) {
        return 1; 
    }

    int dest_is_str = (dest.base == TYPE_STRING && dest.ptr_depth == 0);
    int src_is_str = (src.base == TYPE_STRING && src.ptr_depth == 0);
    
    int dest_is_char_p = (dest.base == TYPE_CHAR && (dest.ptr_depth > 0 || dest.array_size > 0));
    int src_is_char_p = (src.base == TYPE_CHAR && (src.ptr_depth > 0 || src.array_size > 0));

    if ((dest_is_str && src_is_char_p) || (dest_is_char_p && src_is_str)) {
        return 1;
    }
    
    if (src.array_size > 0 && dest.ptr_depth == src.ptr_depth + 1 && dest.base == src.base && dest.vector_depth == src.vector_depth) {
        return 1; 
    }
    
    // Array/Pointer literal to Vector assignment compatibility
    if (dest.base == src.base && dest.vector_depth > 0 && 
        src.ptr_depth == dest.vector_depth && src.vector_depth == 0 && 
        dest.ptr_depth == 0) {
        return 1;
    }

    if (dest.base == TYPE_VOID && dest.ptr_depth > 0 && src.ptr_depth > 0) return 1;

    return 0;
}

char* sem_type_to_str(VarType t) {
    static char buffers[4][256];
    static int idx = 0;
    char *buf = buffers[idx];
    idx = (idx + 1) % 4;

    const char *base = "unknown";
    switch(t.base) {
        case TYPE_INT: base = "int"; break;
        case TYPE_SHORT: base = "short"; break;
        case TYPE_LONG: base = "long"; break;
        case TYPE_LONG_LONG: base = "long long"; break;
        case TYPE_CHAR: base = "char"; break;
        case TYPE_BOOL: base = "bool"; break;
        case TYPE_FLOAT: base = "single"; break;
        case TYPE_DOUBLE: base = "double"; break;
        case TYPE_LONG_DOUBLE: base = "long double"; break;
        case TYPE_VOID: base = "void"; break;
        case TYPE_HASHMAP: base = "hashmap"; break;
        case TYPE_VECTOR: base = "vector"; break;
        case TYPE_ARRAY: base = "array"; break;
        case TYPE_STRING: base = "string"; break;
        case TYPE_AUTO: base = "let"; break;
        case TYPE_CLASS: base = t.class_name ? t.class_name : "class"; break;
        case TYPE_ENUM: base = t.class_name ? t.class_name : "enum"; break;
        default: base = "unknown"; break;
    }
    
    int pos = 0;
    for (int i=0; i<t.vector_depth; i++) {
        pos += snprintf(buf + pos, 256 - pos, "vector ");
    }

    if (t.is_unsigned) pos += snprintf(buf + pos, 256 - pos, "unsigned ");
    pos += snprintf(buf + pos, 256 - pos, "%s", base);
    
    for(int i=0; i<t.ptr_depth; i++) {
        if(pos < 255) buf[pos++] = '*';
    }
    buf[pos] = '\0';
    
    if (t.array_size > 0) {
        char tmp[32];
        snprintf(tmp, 32, "[%d]", t.array_size);
        strcat(buf, tmp);
    }
    
    if (t.is_func_ptr) {
        strcat(buf, "(*)(...)");
    }

    return buf;
}
