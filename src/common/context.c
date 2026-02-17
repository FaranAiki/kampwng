#include "context.h"
#include <string.h>

void context_init(CompilerContext *ctx, Arena *arena) {
    if (!ctx) return;
    
    ctx->arena = arena;
    ctx->has_recovery = 0;
    ctx->error_count = 0;
    
    // Initialize diagnostic state
    // Default namespace is "main"
    strncpy(ctx->current_namespace, "main", 255);
    ctx->current_namespace[255] = '\0';
    
    ctx->last_reported_namespace[0] = '\0';
    ctx->last_reported_filename[0] = '\0';
}
