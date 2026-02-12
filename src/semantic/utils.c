#include "semantic.h"

void mangle_type(char *buf, VarType t) {
    if (t.array_size > 0) {
        sprintf(buf + strlen(buf), "A%d_", t.array_size);
    }
    for(int i=0; i<t.ptr_depth; i++) strcat(buf, "P");
    
    if (t.is_unsigned) strcat(buf, "U");
    
    switch(t.base) {
        case TYPE_INT: strcat(buf, "i"); break;
        case TYPE_SHORT: strcat(buf, "s"); break;
        case TYPE_LONG: strcat(buf, "l"); break;
        case TYPE_LONG_LONG: strcat(buf, "x"); break;
        case TYPE_DOUBLE: strcat(buf, "d"); break;
        case TYPE_FLOAT: strcat(buf, "f"); break;
        case TYPE_LONG_DOUBLE: strcat(buf, "g"); break;
        case TYPE_BOOL: strcat(buf, "b"); break;
        case TYPE_CHAR: strcat(buf, "c"); break;
        case TYPE_VOID: strcat(buf, "v"); break;
        case TYPE_STRING: strcat(buf, "S"); break;
        case TYPE_CLASS: 
            if (t.class_name)
                sprintf(buf + strlen(buf), "C%ld%s", strlen(t.class_name), t.class_name);
            else strcat(buf, "u");
            break;
        default: strcat(buf, "u"); break;
    }
}

void sem_error(SemCtx *ctx, ASTNode *node, const char *fmt, ...) {
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
        l.filename = ctx->filename; 
    }
    
    report_error(ctx->source_code ? &l : NULL, t, msg);
}

char* mangle_function(const char *name, Parameter *params) {
    // Don't mangle main
    if (strcmp(name, "main") == 0) return strdup("main");

    char buf[1024];
    buf[0] = '\0';
    
    // Basic mangling scheme: _Z + len + name + params
    sprintf(buf, "_Z%ld%s", strlen(name), name);
    
    Parameter *p = params;
    while(p) {
        mangle_type(buf, p->type);
        p = p->next;
    }
    
    if (!params) strcat(buf, "v"); // void params
    
    return strdup(buf);
}

const char* type_to_str(VarType t) {
    static char buffers[4][128];
    static int idx = 0;
    char *buf = buffers[idx];
    idx = (idx + 1) % 4;

    char base_buf[64] = "";
    if (t.is_unsigned) strcpy(base_buf, "unsigned ");

    switch (t.base) {
        case TYPE_INT: strcat(base_buf, "int"); break;
        case TYPE_SHORT: strcat(base_buf, "short"); break;
        case TYPE_LONG: strcat(base_buf, "long"); break;
        case TYPE_LONG_LONG: strcat(base_buf, "long long"); break;
        case TYPE_CHAR: strcat(base_buf, "char"); break;
        case TYPE_BOOL: strcat(base_buf, "bool"); break;
        case TYPE_FLOAT: strcat(base_buf, "single"); break;
        case TYPE_DOUBLE: strcat(base_buf, "double"); break;
        case TYPE_LONG_DOUBLE: strcat(base_buf, "long double"); break;
        case TYPE_VOID: strcat(base_buf, "void"); break;
        case TYPE_STRING: strcat(base_buf, "string"); break;
        case TYPE_CLASS: strcat(base_buf, t.class_name ? t.class_name : "class"); break;
        case TYPE_UNKNOWN: strcat(base_buf, "unknown"); break;
        case TYPE_AUTO: strcat(base_buf, "auto"); break;
        default: strcat(base_buf, "???"); break;
    }
    strcpy(buf, base_buf);
    for(int i=0; i<t.ptr_depth; i++) strcat(buf, "*");
    if (t.array_size > 0) {
        char tmp[16]; sprintf(tmp, "[%d]", t.array_size);
        strcat(buf, tmp);
    }
    return buf;
}

const char* find_closest_type_name(SemCtx *ctx, const char *name) {
    const char *primitives[] = {
        "int", "short", "long", "unsigned", "char", "bool", "single", "double", "void", "string", "let", "auto", NULL
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

const char* find_closest_func_name(SemCtx *ctx, const char *name) {
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

const char* find_closest_var_name(SemCtx *ctx, const char *name) {
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

void sem_info(SemCtx *ctx, ASTNode *node, const char *fmt, ...) {
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
        l.filename = ctx->filename;
    }
    report_info(ctx->source_code ? &l : NULL, t, msg);
}

void sem_hint(SemCtx *ctx, ASTNode *node, const char *msg) {
    Token t;
    t.line = node ? node->line : 0;
    t.col = node ? node->col : 0;
    t.text = NULL;
    
    Lexer l;
    if (ctx->source_code) {
        lexer_init(&l, ctx->source_code);
        l.filename = ctx->filename;
    }
    report_hint(ctx->source_code ? &l : NULL, t, msg);
}

void sem_reason(SemCtx *ctx, int line, int col, const char *fmt, ...) {
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
        l.filename = ctx->filename;
    }
    report_reason(ctx->source_code ? &l : NULL, t, msg);
}

void sem_suggestion(SemCtx *ctx, ASTNode *node, const char *suggestion) {
    Token t;
    t.line = node ? node->line : 0;
    t.col = node ? node->col : 0;
    t.text = NULL;
    
    Lexer l;
    if (ctx->source_code) {
        lexer_init(&l, ctx->source_code);
        l.filename = ctx->filename;
    }
    report_hint(ctx->source_code ? &l : NULL, t, suggestion);
}

int are_types_equal(VarType a, VarType b) {
    if (a.ptr_depth > 0 && b.ptr_depth > 0) {
        if (a.base == TYPE_VOID || b.base == TYPE_VOID) return 1;
    }

    if (a.base != b.base || a.is_unsigned != b.is_unsigned) {
        if (a.base == TYPE_AUTO || b.base == TYPE_AUTO) return 1;
        if (a.base == TYPE_STRING && b.base == TYPE_STRING) return 1;
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

int get_conversion_cost(VarType from, VarType to) {
    if (are_types_equal(from, to)) return 0;
    
    if (from.ptr_depth == 0 && to.ptr_depth == 0) {
        // Evaluate type rank for numeric conversions
        int from_rank = 0, to_rank = 0;
        switch(from.base) {
            case TYPE_BOOL: from_rank = 1; break;
            case TYPE_CHAR: from_rank = 2; break;
            case TYPE_SHORT: from_rank = 3; break;
            case TYPE_INT: from_rank = 4; break;
            case TYPE_LONG: from_rank = 5; break;
            case TYPE_LONG_LONG: from_rank = 6; break;
            case TYPE_FLOAT: from_rank = 7; break;
            case TYPE_DOUBLE: from_rank = 8; break;
            case TYPE_LONG_DOUBLE: from_rank = 9; break;
            default: from_rank = 0; break;
        }
        switch(to.base) {
            case TYPE_BOOL: to_rank = 1; break;
            case TYPE_CHAR: to_rank = 2; break;
            case TYPE_SHORT: to_rank = 3; break;
            case TYPE_INT: to_rank = 4; break;
            case TYPE_LONG: to_rank = 5; break;
            case TYPE_LONG_LONG: to_rank = 6; break;
            case TYPE_FLOAT: to_rank = 7; break;
            case TYPE_DOUBLE: to_rank = 8; break;
            case TYPE_LONG_DOUBLE: to_rank = 9; break;
            default: to_rank = 0; break;
        }
        
        if (from_rank > 0 && to_rank > 0) {
            if (from_rank < to_rank) return 1; // Widening
            if (from_rank > to_rank) return 2; // Narrowing
            if (from.is_unsigned != to.is_unsigned) return 2; // Signedness change penalty
        }
    }
    
    if (from.base == TYPE_STRING && from.ptr_depth == 0) {
        if (to.base == TYPE_CHAR && to.ptr_depth == 1) return 1;
    }
    
    return -1;
}
