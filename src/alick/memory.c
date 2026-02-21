#include "../../include/alick/alick_internal.h"
#include "../../include/common/hashmap.h"

// Helper to extract a string identifier for an AlirValue (Temp IDs or Var names)
static void get_val_key(AlirValue *val, char *out_key) {
    if (!val) {
        out_key[0] = '\0';
        return;
    }
    if (val->kind == ALIR_VAL_TEMP) {
        sprintf(out_key, "%%t%d", val->temp_id);
    } else if (val->kind == ALIR_VAL_VAR) {
        sprintf(out_key, "@%s", val->str_val);
    } else {
        out_key[0] = '\0';
    }
}

// Memory Safety Pass (Block-Local)
// Detects basic Use-After-Free and Double-Free vulnerabilities within single blocks.
void alick_check_memory(AlickCtx *ctx, AlirFunction *func) {
    AlirBlock *b = func->blocks;
    
    while (b) {
        // Track pointers freed in this block to catch UAF
        HashMap freed_map;
        
        // Use arena if available to avoid memory leaks if checking bails early
        Arena *arena = NULL;
        if (ctx->module->compiler_ctx && ctx->module->compiler_ctx->arena) {
            arena = ctx->module->compiler_ctx->arena;
        }
        
        hashmap_init(&freed_map, arena, 32);

        AlirInst *i = b->head;
        while (i) {
            char key1[64] = {0};
            char key2[64] = {0};
            
            get_val_key(i->op1, key1);
            get_val_key(i->op2, key2);

            // 1. Check Use-After-Free on operands
            if (key1[0] != '\0' && hashmap_has(&freed_map, key1)) {
                alick_error(ctx, func, b, i, "Use-After-Free: Pointer/Variable '%s' is used after being freed.", key1);
            }
            if (key2[0] != '\0' && hashmap_has(&freed_map, key2)) {
                alick_error(ctx, func, b, i, "Use-After-Free: Pointer/Variable '%s' is used after being freed.", key2);
            }

            // Also check call arguments for UAF
            for (int arg_idx = 0; arg_idx < i->arg_count; arg_idx++) {
                char arg_key[64] = {0};
                get_val_key(i->args[arg_idx], arg_key);
                if (arg_key[0] != '\0' && hashmap_has(&freed_map, arg_key)) {
                    alick_error(ctx, func, b, i, "Use-After-Free: Pointer '%s' is passed as argument after being freed.", arg_key);
                }
            }

            // 2. Track FREE instructions
            if (i->op == ALIR_OP_FREE && key1[0] != '\0') {
                if (hashmap_has(&freed_map, key1)) {
                    alick_error(ctx, func, b, i, "Double-Free: Pointer '%s' is freed multiple times.", key1);
                } else {
                    hashmap_put(&freed_map, key1, (void*)1);
                }
            }

            i = i->next;
        }

        if (!arena) {
            hashmap_free(&freed_map);
        }

        b = b->next;
    }
}
