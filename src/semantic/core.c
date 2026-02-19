#include "semantic.h"
#include <stdarg.h>
#include <stdio.h>

// Helper to construct a temporary lexer for reporting
static void setup_report_lexer(Lexer *l, SemanticCtx *ctx) {
    if (ctx->compiler_ctx) {
        // Initialize with the context and source
        // We assume lexer_init handles basic setup
        lexer_init(l, ctx->compiler_ctx, ctx->current_filename, ctx->current_source);
    } else {
        // Fallback if no compiler context (shouldn't happen in proper flow)
        l->ctx = NULL;
        l->src = ctx->current_source;
        l->filename = (char*)ctx->current_filename;
    }
}

void sem_hint(SemanticCtx *ctx, ASTNode *node, const char *fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (ctx->current_source && node) {
        Lexer l;
        setup_report_lexer(&l, ctx);
        
        Token t;
        t.line = node->line;
        t.col = node->col;
        t.type = TOKEN_UNKNOWN; 
        t.text = NULL;
        
        report_hint(&l, t, msg);
    } else {
        fprintf(stderr, "[Semantic Hint] %s\n", msg);
    }
}

void sem_error(SemanticCtx *ctx, ASTNode *node, const char *fmt, ...) {
    if (ctx->compiler_ctx) {
        ctx->compiler_ctx->error_count++;
        ctx->compiler_ctx->semantic_error_count++;
    }
    
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (ctx->current_source && node) {
        Lexer l;
        setup_report_lexer(&l, ctx);
        
        Token t;
        t.line = node->line;
        t.col = node->col;
        t.type = TOKEN_UNKNOWN; 
        t.text = NULL;
        t.int_val = 0; 
        t.double_val = 0.0;
        
        report_error(&l, t, msg);
    } else {
        if (node) {
            fprintf(stderr, "[Semantic Error] Line %d, Col %d: %s\n", node->line, node->col, msg);
        } else {
            fprintf(stderr, "[Semantic Error] %s\n", msg);
        }
    }
}

void sem_info(SemanticCtx *ctx, ASTNode *node, const char *fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (ctx->current_source && node) {
        Lexer l;
        setup_report_lexer(&l, ctx);
        
        Token t;
        t.line = node->line;
        t.col = node->col;
        t.type = TOKEN_UNKNOWN; 
        t.text = NULL;
        
        report_info(&l, t, msg);
    } else {
        fprintf(stderr, "[Semantic Info] %s\n", msg);
    }
}

int sem_check_program(SemanticCtx *ctx, ASTNode *root) {
    if (!root) return 0;
    
    sem_register_builtins(ctx);
    sem_scan_top_level(ctx, root);
    
    int current_errors = 0;
    if (ctx->compiler_ctx) current_errors = ctx->compiler_ctx->semantic_error_count;

    if (current_errors > 0) return current_errors;
    
    // Pass 1.5: Structural validations (Inheritance, Traits)
    ASTNode *curr_val = root;
    while (curr_val) {
        if (curr_val->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)curr_val;
            if (cn->parent_name) {
                SemSymbol *parent = sem_symbol_lookup(ctx, cn->parent_name, NULL);
                if (parent && parent->kind == SYM_CLASS) {
                    if (parent->is_is_a == IS_A_FINAL) {
                        sem_error(ctx, curr_val, "Class '%s' cannot inherit from final class '%s'", cn->name, cn->parent_name);
                    }
                    parent->is_used_as_parent = 1;
                }
            }
            for (int i = 0; i < cn->traits.count; i++) {
                SemSymbol *trait = sem_symbol_lookup(ctx, cn->traits.names[i], NULL);
                if (trait && trait->kind == SYM_CLASS) {
                    if (trait->is_has_a == HAS_A_INERT) {
                        sem_error(ctx, curr_val, "Class '%s' is inert, thus cannot be explicitly composed in '%s'", trait->name, cn->name);
                    }
                    trait->is_used_as_composition = 1;
                }
            }
        }
        curr_val = curr_val->next;
    }

    ASTNode *curr = root;
    while (curr) {
        if (curr->type == NODE_VAR_DECL) {
            // Check global var initializers (don't register, already scanned)
            sem_check_var_decl(ctx, (VarDeclNode*)curr, 0);
        } else {
            sem_check_node(ctx, curr);
        }
        curr = curr->next;
    }
    
    // Final structural validations (naked, reactive) globally
    if (ctx->global_scope) {
        SemSymbol *gsym = ctx->global_scope->symbols;
        while (gsym) {
            if (gsym->kind == SYM_CLASS) {
                if (gsym->is_is_a == IS_A_NAKED && !gsym->is_used_as_parent) {
                    sem_error(ctx, NULL, "Class '%s' is marked naked but is never inherited", gsym->name);
                }
                if (gsym->is_has_a == HAS_A_REACTIVE && !gsym->is_used_as_composition) {
                    sem_error(ctx, NULL, "Class '%s' is marked reactive but is never composed (has-ed)", gsym->name);
                }
            }
            gsym = gsym->next;
        }
    }

    if (ctx->compiler_ctx) return ctx->compiler_ctx->semantic_error_count;
    return 0;
}
