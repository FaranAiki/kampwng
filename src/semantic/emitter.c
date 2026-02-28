#include "../../include/semantic/emitter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void semantic_emit_indent(StringBuilder *sb, int indent) {
    for (int i = 0; i < indent; i++) sb_append_fmt(sb, "  ");
}

void semantic_emit_type_str(StringBuilder *sb, VarType t) {
    // 1. Explicitly mark Vectors
    for (int i = 0; i < t.vector_depth; i++) sb_append_fmt(sb, "vector ");
    
    if (t.is_unsigned) sb_append_fmt(sb, "unsigned ");
    
    // 2. Explicitly handle C-Strings vs Char
    if (t.base == TYPE_CHAR && t.ptr_depth > 0) {
        sb_append_fmt(sb, "c-string");
        // Decrement by 1 since we consumed one pointer depth for the c-string itself
        for (int i = 0; i < t.ptr_depth - 1; i++) sb_append_fmt(sb, "*");
        
        if (t.array_size > 0) sb_append_fmt(sb, " array[%d]", t.array_size);
        return;
    }
    
    switch (t.base) {
        case TYPE_INT: sb_append_fmt(sb, "int"); break;
        case TYPE_SHORT: sb_append_fmt(sb, "short"); break;
        case TYPE_LONG: sb_append_fmt(sb, "long"); break;
        case TYPE_LONG_LONG: sb_append_fmt(sb, "long long"); break;
        case TYPE_CHAR: sb_append_fmt(sb, "char"); break; // Single character
        case TYPE_BOOL: sb_append_fmt(sb, "bool"); break;
        case TYPE_FLOAT: sb_append_fmt(sb, "single"); break;
        case TYPE_DOUBLE: sb_append_fmt(sb, "double"); break;
        case TYPE_LONG_DOUBLE: sb_append_fmt(sb, "long double"); break;
        case TYPE_VOID: sb_append_fmt(sb, "void"); break;
        case TYPE_STRING: sb_append_fmt(sb, "string"); break; // Native String
        case TYPE_AUTO: sb_append_fmt(sb, "let"); break;
        case TYPE_CLASS: sb_append_fmt(sb, "%s", t.class_name ? t.class_name : "class"); break;
        case TYPE_ENUM: sb_append_fmt(sb, "enum %s", t.class_name ? t.class_name : ""); break;
        case TYPE_NAMESPACE: sb_append_fmt(sb, "namespace %s", t.class_name ? t.class_name : ""); break;
        case TYPE_ARRAY: sb_append_fmt(sb, "array"); break;
        case TYPE_VECTOR: sb_append_fmt(sb, "vector"); break;
        // TODO type hashmap, type_unknown
        default: sb_append_fmt(sb, "unknown"); break;
    }

    // Output generic pointers
    for (int i = 0; i < t.ptr_depth; i++) sb_append_fmt(sb, "*");
    
    // 3. Emphasize explicit arrays
    if (t.array_size > 0) {
        sb_append_fmt(sb, " array[%d]", t.array_size);
    } else if (t.array_size == -1) {
        sb_append_fmt(sb, " array[]");
    }
}

void semantic_emit_symbol(StringBuilder *sb, SemSymbol *sym, int indent) {
    semantic_emit_indent(sb, indent);
    
    const char *kind_str = "UNK";
    switch (sym->kind) {
        case SYM_VAR: kind_str = "VAR"; break;
        case SYM_FUNC: kind_str = "FUNC"; break;
        case SYM_CLASS: kind_str = "CLASS"; break;
        case SYM_ENUM: kind_str = "ENUM"; break;
        case SYM_NAMESPACE: kind_str = "NAMESPACE"; break;
    }
    
    sb_append_fmt(sb, "[%s] %s : ", kind_str, sym->name);
    semantic_emit_type_str(sb, sym->type);
    
    if (sym->parent_name) {
        sb_append_fmt(sb, " (extends %s)", sym->parent_name);
    }
    
    sb_append_fmt(sb, "\n");

    if (sym->inner_scope) {
        semantic_emit_scope(sb, sym->inner_scope, indent + 1);
    }
}

void semantic_emit_scope(StringBuilder *sb, SemScope *scope, int indent) {
    if (!scope) return;
    
    SemSymbol *sym = scope->symbols;
    if (!sym) {
        semantic_emit_indent(sb, indent);
        sb_append_fmt(sb, "(empty scope)\n");
        return;
    }

    while (sym) {
        semantic_emit_symbol(sb, sym, indent);
        sym = sym->next;
    }
}

char* semantic_to_string(SemanticCtx *ctx) {
    StringBuilder sb;
    sb_init(&sb, ctx->compiler_ctx->arena);
    
    sb_append_fmt(&sb, "=== SEMANTIC SYMBOL TABLE ===\n");
    if (ctx->global_scope) {
        semantic_emit_scope(&sb, ctx->global_scope, 0);
    } else {
        sb_append_fmt(&sb, "No global scope initialized.\n");
    }
    sb_append_fmt(&sb, "=============================\n");
    
    return sb.data;
}

void semantic_to_file(SemanticCtx *ctx, const char *filename) {
    char *str = semantic_to_string(ctx);
    if (str) {
        FILE *f = fopen(filename, "w");
        if (f) {
            fputs(str, f);
            fclose(f);
        }
    }
}
