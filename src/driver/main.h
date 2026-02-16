#ifndef MAIN_H
#define MAIN_H

#include <llvm-c/Core.h>
#include <stdbool.h>
#include "../semantic/semantic.h"
#include "../parser/parser.h"
#include "../codegen/codegen.h"
#include "../compiler_debug/compiler_debug.h"

char* read_file(const char* filename);

#endif
