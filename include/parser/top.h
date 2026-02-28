#ifndef PARSER_TOP_H
#define PARSER_TOP_H

#include "parser_internal.h"

ASTNode* parse_top_level(Parser *p); 
ASTNode* parse_enum(Parser *p);
ASTNode* parse_class(Parser *p);
ASTNode* parse_define(Parser *p);
ASTNode* parse_typedef(Parser *p);
ASTNode* parse_link(Parser *p);
ASTNode* parse_extern(Parser *p, int modifier);

#endif // PARSER_TOP_H
