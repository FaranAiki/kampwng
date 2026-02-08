#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "../parser/parser.h"

int semantic_analysis(ASTNode *root, const char *source, const char *filename);

#endif
