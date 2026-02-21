#ifndef ALICK_H
#define ALICK_H

#include "../alir/alir.h"

// Context for tracking errors during ALIR checking
typedef struct AlickCtx {
    AlirModule *module;
    int error_count;
    int warning_count;
} AlickCtx;

// Main entry point to check an ALIR module
// Runs CFG, Structure/Type, and basic Memory/Lifetime passes.
// Returns the number of errors found (0 means success and valid IR).
int alick_check_module(AlirModule *mod);

#endif // ALICK_H
