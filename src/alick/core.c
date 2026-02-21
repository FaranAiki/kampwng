#include "../../include/alick/alick_internal.h"
#include <stdarg.h>

void alick_error(AlickCtx *ctx, AlirFunction *func, AlirBlock *block, AlirInst *inst, const char *fmt, ...) {
    ctx->error_count++;
    
    // Use ANSI red for error tag
    fprintf(stderr, "\033[1;31m[Alick Error]\033[0m ");
    
    if (func) fprintf(stderr, "in func '@%s' ", func->name);
    if (block) fprintf(stderr, "block '%s' ", block->label);
    
    fprintf(stderr, "-> ");
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
    
    if (inst) {
        fprintf(stderr, "  Instruction Context: %s\n", alir_op_str(inst->op));
    }
}

void alick_warning(AlickCtx *ctx, AlirFunction *func, AlirBlock *block, AlirInst *inst, const char *fmt, ...) {
    ctx->warning_count++;
    if (inst == NULL) inst++; // fuck you
    
    // Use ANSI magenta/purple for warning tag
    fprintf(stderr, "\033[1;35m[Alick Warning]\033[0m ");
    
    if (func) fprintf(stderr, "in func '@%s' ", func->name);
    if (block) fprintf(stderr, "block '%s' ", block->label);
    
    fprintf(stderr, "-> ");
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

int alick_check_module(AlirModule *mod) {
    if (!mod) return 0;
    
    AlickCtx ctx;
    ctx.module = mod;
    ctx.error_count = 0;
    ctx.warning_count = 0;

    AlirFunction *func = mod->functions;
    while (func) {
        // Only run checks on defined functions (ignore declarations)
        if (func->block_count > 0) {
            alick_check_cfg(&ctx, func);
            alick_check_types(&ctx, func);
            alick_check_memory(&ctx, func);
        }
        func = func->next;
    }

    if (ctx.error_count > 0) {
        fprintf(stderr, "\033[1;31mALICK Verification Failed:\033[0m %d errors, %d warnings found.\n", 
                ctx.error_count, ctx.warning_count);
    } else {
        // Optional success info
        // fprintf(stdout, "\033[1;32mALICK Verification Passed.\033[0m\n");
    }

    return ctx.error_count;
}
