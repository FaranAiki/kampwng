#ifndef PARSER_STMT_H 
#define PARSER_STMT_H

#include "parser_internal.h"

ASTNode* parse_single_statement_or_block(Parser *p);
ASTNode* parse_statements(Parser *p);
ASTNode* parse_var_decl_internal(Parser *p);
ASTNode* parse_assignment_or_call(Parser *p);
ASTNode* parse_loop(Parser *p);
ASTNode* parse_while(Parser *p);
ASTNode* parse_if(Parser *p);
ASTNode* parse_switch(Parser *p); 
ASTNode* parse_return(Parser *p);
ASTNode* parse_break(Parser *p);
ASTNode* parse_continue(Parser *p);
ASTNode* parse_emit(Parser *p);
ASTNode* parse_for_in(Parser *p);

#endif // PARSER_STMT_H
