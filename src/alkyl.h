#ifndef KAMPWNG_H
#define KAMPWNG_H

#include <llvm-c/Core.h>
#include <stdbool.h>

// Import definitions from parser.h (which imports lexer.h)
// This prevents redefinition errors and ensures all files use the same enums
#include "parser.h"

// --- CODEGEN PROTOTYPES ---

LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name);

#endif
