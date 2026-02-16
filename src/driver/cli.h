#ifndef CLI_H
#define CLI_H

#define INPUT_BUFFER_SIZE 4096

#include "../parser/parser.h"
#include "../.old/codegen/codegen.h"
#include "../compiler_debug/compiler_debug.h"

// Entry point for the Interactive Shell
int run_repl(void);

#endif
