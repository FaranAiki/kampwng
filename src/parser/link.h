#ifndef PARSER_LINK_H
#define PARSER_LINK_H

#include "parser_internal.h"

ASTNode* parse_import(Parser *p);
ASTNode* parse_link(Parser *p);

#endif // PARSER_LINK_H
