#ifndef ALICK_INTERNAL_H
#define ALICK_INTERNAL_H

#include "alick.h"
#include <stdio.h>

// Shared internal logging utility for emitting verification errors
void alick_error(AlickCtx *ctx, AlirFunction *func, AlirBlock *block, AlirInst *inst, const char *fmt, ...);
void alick_warning(AlickCtx *ctx, AlirFunction *func, AlirBlock *block, AlirInst *inst, const char *fmt, ...);

// --- Passes ---

// Pass 1: Control Flow Graph (CFG) Validation
void alick_check_cfg(AlickCtx *ctx, AlirFunction *func);

// Pass 2: Type, Operands, and Structural Validation
void alick_check_types(AlickCtx *ctx, AlirFunction *func);

// Pass 3: Localized Memory Validity (Dangling Pointers, UAF, Double-Free)
void alick_check_memory(AlickCtx *ctx, AlirFunction *func);

#endif // ALICK_INTERNAL_H
