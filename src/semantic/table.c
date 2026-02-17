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
    sym->parent_name = NULL; // Initialize parent_name
    sym->is_mutable = 1; 
    sym->is_initialized = 1; // Default to true (safe for params/funcs), manual unset for uninit vars
    sym->inner_scope = NULL;
    
    sym->next = ctx->current_scope->symbols;
    ctx->current_scope->symbols = sym;
    return sym;
}

SemSymbol* sem_symbol_lookup(SemanticCtx *ctx, const char *name) {
    SemScope *scope = ctx->current_scope;
    while (scope) {
        SemSymbol *sym = scope->symbols;
        // Pass 1: Direct lookup for variables, functions, or the Enum type itself
        while (sym) {
            if (strcmp(sym->name, name) == 0) return sym;
            sym = sym->next;
        }

        // Pass 2: Implicit Enum Member Lookup
        // If not found directly, check inside any Enums defined in this scope.
        // This allows 'Daging' to be found if 'Makanan' is in scope.
        sym = scope->symbols;
        while (sym) {
            if (sym->kind == SYM_ENUM && sym->inner_scope) {
                SemSymbol *mem = sym->inner_scope->symbols;
                while (mem) {
                    if (strcmp(mem->name, name) == 0) return mem;
                    mem = mem->next;
                }
            }
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

    // Auto resolution (allowed target)
    if (dest.base == TYPE_AUTO) return 1; 

    // Numeric promotions
    int dest_is_num = (dest.base >= TYPE_INT && dest.base <= TYPE_LONG_DOUBLE);
    int src_is_num = (src.base >= TYPE_INT && src.base <= TYPE_LONG_DOUBLE);

    // Enums are implicitly compatible with integers and themselves
    if (src.base == TYPE_ENUM && dest_is_num) return 1;
    if (dest.base == TYPE_ENUM && src_is_num) return 1;
    
    if (dest_is_num && src_is_num && dest.ptr_depth == 0 && src.ptr_depth == 0) {
        return 1; // Allow implicit numeric casts
    }

    // String <-> Char* / Char[] Implicit Compatibility
    // Allows implicit cast between 'string' and 'char*' or 'char[]'
    int dest_is_str = (dest.base == TYPE_STRING && dest.ptr_depth == 0);
    int src_is_str = (src.base == TYPE_STRING && src.ptr_depth == 0);
    
    int dest_is_char_p = (dest.base == TYPE_CHAR && (dest.ptr_depth > 0 || dest.array_size > 0));
    int src_is_char_p = (src.base == TYPE_CHAR && (src.ptr_depth > 0 || src.array_size > 0));

    if ((dest_is_str && src_is_char_p) || (dest_is_char_p && src_is_str)) {
        return 1;
    }
    
    // Array decay (int[10] -> int*)
    // src is array, dest is pointer, base types match, depth matches decay
    if (src.array_size > 0 && dest.ptr_depth == src.ptr_depth + 1 && dest.base == src.base) {
        return 1; 
    }

    // void* generic
    if (dest.base == TYPE_VOID && dest.ptr_depth > 0 && src.ptr_depth > 0) return 1;

    return 0;
}

char* sem_type_to_str(VarType t) {
    // Robustness Fix: Use a rotating buffer pool so this function can be called 
    // multiple times in a single printf/error call without overwriting results.
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
        case TYPE_STRING: base = "string"; break;
        case TYPE_AUTO: base = "let"; break;
        case TYPE_CLASS: base = t.class_name ? t.class_name : "class"; break;
        case TYPE_ENUM: base = t.class_name ? t.class_name : "enum"; break;
        default: base = "unknown"; break;
    }
    
    int pos = 0;
    if (t.is_unsigned) pos += snprintf(buf + pos, 256 - pos, "unsigned ");
    pos += snprintf(buf + pos, 256 - pos, "%s", base);
    
    // Append pointers
    for(int i=0; i<t.ptr_depth; i++) {
        if(pos < 255) buf[pos++] = '*';
    }
    buf[pos] = '\0';
    
    // Append array size if present
    if (t.array_size > 0) {
        char tmp[32];
        snprintf(tmp, 32, "[%d]", t.array_size);
        strcat(buf, tmp);
    }
    
    // Simple function pointer notation
    if (t.is_func_ptr) {
        strcat(buf, "(*)(...)");
    }

    return buf;
}
