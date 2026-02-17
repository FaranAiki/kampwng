#ifndef MAIN_H
#define MAIN_H

#include <llvm-c/Core.h>
#include <stdbool.h>
#include "../old/codegen/codegen.h"
#include "../parser/parser.h"
#include "../semantic/semantic.h"
#include "../common/compiler_debug.h"
#include "../alir/alir.h"
// #include "../alick/alick.h"

char* read_file(const char* filename);

#endif
