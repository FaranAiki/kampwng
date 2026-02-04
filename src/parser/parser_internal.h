#ifndef PARSER_INTERNAL_H
#define PARSER_INTERNAL_H

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

// --- SHARED GLOBALS ---
extern Token current_token;
extern jmp_buf *parser_env;

// --- CORE FUNCTIONS (parser_core.c) ---
void parser_fail(const char *msg);
void eat(Lexer *l, TokenType type);
VarType parse_type(Lexer *l); 
char* read_import_file(const char* filename);

// Macro Registry
void register_macro(const char *name, char **params, int param_count, Token *body, int body_len);

// Type Registry (OOP)
void register_typename(const char *name);
int is_typename(const char *name);

// helper to allow top.c to peek/consume raw tokens for define parsing
Token lexer_next_raw(Lexer *l); 

// --- EXPRESSION PARSERS (parser_expr.c) ---
ASTNode* parse_call(Lexer *l, char *name);
ASTNode* parse_postfix(Lexer *l, ASTNode *node); // Exposed for stmt.c

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
