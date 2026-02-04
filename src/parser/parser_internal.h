#ifndef PARSER_INTERNAL_H
#define PARSER_INTERNAL_H

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

// --- SHARED GLOBALS ---
// Defined in parser_core.c
extern Token current_token;
extern jmp_buf *parser_env;

// --- CORE FUNCTIONS (parser_core.c) ---
void parser_fail(const char *msg);
void eat(Lexer *l, TokenType type);
VarType get_type_from_token(TokenType t);
char* read_import_file(const char* filename);

// --- EXPRESSION PARSERS (parser_expr.c) ---
// parse_expression is already in parser.h
ASTNode* parse_call(Lexer *l, char *name);

// --- STATEMENT PARSERS (parser_stmt.c) ---
ASTNode* parse_single_statement_or_block(Lexer *l);
ASTNode* parse_statements(Lexer *l);
ASTNode* parse_var_decl_internal(Lexer *l);
ASTNode* parse_assignment_or_call(Lexer *l);
ASTNode* parse_loop(Lexer *l);
ASTNode* parse_while(Lexer *l);
ASTNode* parse_if(Lexer *l);
ASTNode* parse_return(Lexer *l);
ASTNode* parse_break(Lexer *l);
ASTNode* parse_continue(Lexer *l);

#endif
