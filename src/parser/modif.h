#ifndef PARSER_MODIF_H
#define PARSER_MODIF_H

#include "parser_internal.h"

typedef struct MacroSig {
    char *name;
    char **params;
    int param_count;
} MacroSig;

ASTNode* parse_define(Parser *p);
ASTNode* parse_typedef(Parser *p);

#endif // PARSER_MODIF_H
