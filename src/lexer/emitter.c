#include "emitter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../common/arena.h"
#include "../common/context.h"

char* lexer_to_string(Lexer *l) {
    StringBuilder sb;
    sb_init(&sb);
    if (!sb.data) return NULL;

    Token t = lexer_next(l);
    while (t.type != TOKEN_EOF) {
        const char *type_str = token_type_to_string(t.type);
        
        sb_append_fmt(&sb, "[%-15s] ", type_str);
        
        // Value info
        if (t.text) {
            sb_append_fmt(&sb, "'%s'", t.text);
        } else if (t.type == TOKEN_NUMBER) {
            sb_append_fmt(&sb, "%d", t.int_val);
        } else if (t.type == TOKEN_FLOAT) {
            sb_append_fmt(&sb, "%f", t.double_val);
        } else if (t.type == TOKEN_STRING) {
            sb_append_fmt(&sb, "\"%s\"", t.text ? t.text : "");
        }

        sb_append_fmt(&sb, "\t(Line: %d, Col: %d)\n", t.line, t.col);
        
        t = lexer_next(l);
    }
    
    return sb_free_and_return(&sb);
}

void lexer_to_file(Lexer *l, const char *filename) {
    char *str = lexer_to_string(l);
    if (str) {
        FILE *f = fopen(filename, "w");
        if (f) {
            fputs(str, f);
            fclose(f);
        }
        free(str); // str is allocated by StringBuilder (malloc), so we free it.
    }
}

char* lexer_string_to_string(const char *src) {
    // 1. Setup temporary environment
    Arena arena;
    arena_init(&arena);
    
    CompilerContext ctx;
    context_init(&ctx, &arena);

    // 2. Setup Lexer
    Lexer l;
    lexer_init(&l, src);
    
    // 3. Process
    char* result = lexer_to_string(&l);
    
    // 4. Cleanup
    // result is malloc'd by StringBuilder, so it survives arena_free
    arena_free(&arena);
    
    return result;
}

void lexer_string_to_file(const char *src, const char *filename) {
    Arena arena;
    arena_init(&arena);
    
    CompilerContext ctx;
    context_init(&ctx, &arena);

    Lexer l;
    lexer_init(&l, src);
    
    lexer_to_file(&l, filename);
    
    arena_free(&arena);
}
