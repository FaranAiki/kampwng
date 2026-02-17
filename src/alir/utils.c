#include "alir.h"

AlirInst* mk_inst(AlirOpcode op, AlirValue *dest, AlirValue *op1, AlirValue *op2) {
    AlirInst *i = calloc(1, sizeof(AlirInst));
    i->op = op;
    i->dest = dest;
    i->op1 = op1;
    i->op2 = op2;
    return i;
}

void emit(AlirCtx *ctx, AlirInst *i) {
    if (!ctx->current_block) return;
    alir_append_inst(ctx->current_block, i);
}

AlirValue* new_temp(AlirCtx *ctx, VarType t) {
    return alir_val_temp(t, ctx->temp_counter++);
}

AlirValue* promote(AlirCtx *ctx, AlirValue *v, VarType target) {
    // Basic Promotion Logic: Check base types
    if (v->type.base == target.base && v->type.ptr_depth == target.ptr_depth) return v;
    
    AlirValue *dest = new_temp(ctx, target);
    emit(ctx, mk_inst(ALIR_OP_CAST, dest, v, NULL));
    return dest;
}

// Symbol Table (IR Level: Maps names to Allocas/Registers)
void alir_add_symbol(AlirCtx *ctx, const char *name, AlirValue *ptr, VarType t) {
    AlirSymbol *s = calloc(1, sizeof(AlirSymbol));
    s->name = strdup(name);
    s->ptr = ptr;
    s->type = t;
    s->next = ctx->symbols;
    ctx->symbols = s;
}

AlirSymbol* alir_find_symbol(AlirCtx *ctx, const char *name) {
    AlirSymbol *s = ctx->symbols;
    while(s) {
        if (strcmp(s->name, name) == 0) return s;
        s = s->next;
    }
    return NULL;
}
