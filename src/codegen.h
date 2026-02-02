#ifndef CODEGEN_H
#define CODEGEN_H

#include <llvm-c/Core.h>
#include "parser.h"

// --- PROTOTYPES ---

LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name);

#endif
