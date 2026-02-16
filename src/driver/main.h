#ifndef MAIN_H
#define MAIN_H

#include <llvm-c/Core.h>
#include <stdbool.h>
#include "../.old/semantic/semantic.h"
#include "../.old/codegen/codegen.h"
#include "../parser/parser.h"
#include "../compiler_debug/compiler_debug.h"
#include "../alir/alir.h"

char* read_file(const char* filename);

#endif
