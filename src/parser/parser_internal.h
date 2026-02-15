#ifndef PARSER_INTERNAL_H
#define PARSER_INTERNAL_H

#include "parser.h"
#include "../diagnostic/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

// --- SHARED GLOBALS ---
extern Token current_token;
extern jmp_buf *parser_env;      // For REPL recovery
extern jmp_buf *parser_recover_buf; // For file parsing recovery
extern int parser_error_count;   // Total errors found

// --- CORE FUNCTIONS (parser_core.c) ---
// Report error at current token
void parser_fail(Lexer *l, const char *msg);
// Report error at specific token (fix for caret position)
void parser_fail_at(Lexer *l, Token t, const char *msg);
// Skip tokens until a sync point is found
void parser_sync(Lexer *l);

void eat(Lexer *l, TokenType type);
VarType parse_type(Lexer *l); 
// Parses "(*name)(params)" logic, returning the composite Type and filling out_name
VarType parse_func_ptr_decl(Lexer *l, VarType ret_type, char **out_name);

char* read_import_file(const char* filename);

// Macro Registry
void register_macro(const char *name, char **params, int param_count, Token *body, int body_len);

// Type Registry (OOP)
void register_typename(const char *name, int is_enum);
int is_typename(const char *name);

// Alias Registry (Typedef)
void register_alias(const char *name, VarType target);
VarType* get_alias(const char *name);

Token lexer_next_raw(Lexer *l); 

// parse expr
ASTNode* parse_call(Lexer *l, char *name);
ASTNode* parse_postfix(Lexer *l, ASTNode *node); 

// parse stmt
ASTNode* parse_single_statement_or_block(Lexer *l);
ASTNode* parse_statements(Lexer *l);
ASTNode* parse_var_decl_internal(Lexer *l);
ASTNode* parse_assignment_or_call(Lexer *l);
ASTNode* parse_loop(Lexer *l);
ASTNode* parse_while(Lexer *l);
ASTNode* parse_if(Lexer *l);
ASTNode* parse_switch(Lexer *l); 
ASTNode* parse_return(Lexer *l);
ASTNode* parse_break(Lexer *l);
ASTNode* parse_continue(Lexer *l);

// Flux Parsing
ASTNode* parse_emit(Lexer *l);
ASTNode* parse_for_in(Lexer *l);

// parse top level 
ASTNode* parse_top_level(Lexer *l); 

ASTNode* parse_enum(Lexer *l);
ASTNode* parse_class(Lexer *l);
ASTNode* parse_define(Lexer *l);
ASTNode* parse_typedef(Lexer *l);
ASTNode* parse_import(Lexer *l);
ASTNode* parse_link(Lexer *l);

#endif
