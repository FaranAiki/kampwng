#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include "../../include/llvm_codegen/codegen.h"
#include "../../include/parser/parser.h"
#include "../../include/semantic/semantic.h"
#include "../../include/parser/parser.h"
#include "../../include/semantic/semantic.h"
#include "../../include/common/debug.h"
#include "../../include/alir/alir.h"
#include "../include/alick/alick.h"

char* read_file(const char* filename);

#endif
