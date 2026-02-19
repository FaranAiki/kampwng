#ifndef CLI_H
#define CLI_H

#define INPUT_BUFFER_SIZE 4096

#include "../../include/parser/parser.h"
#include "../../src/old/codegen/codegen.h"
#include "../../include/common/debug.h"

// Entry point for the Interactive Shell
int run_repl(void);

#endif
