#ifndef PARSER_INTERNAL_H
#define PARSER_INTERNAL_H

#include "parser.h"
#include "../common/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

// --- INTERNAL DATA STRUCTURES ---

struct Macro {
    char *name;
    char **params;
    int param_count;
    Token *body;
    int body_len;
    struct Macro *next;
};

struct TypeName {
    char *name;
    int is_enum; 
    struct TypeName *next;
};

struct TypeAlias {
    char *name;
    VarType target;
    struct TypeAlias *next;
};

struct Expansion {
    Token *tokens;
    int count;
    int pos;
    struct Expansion *next;
};

void parser_fail(Parser *p, const char *msg);
void parser_fail_at(Parser *p, Token t, const char *msg);
void parser_sync(Parser *p);

void eat(Parser *p, TokenType type);
VarType parse_type(Parser *p); 
VarType parse_func_ptr_decl(Parser *p, VarType ret_type, char **out_name);

char* read_import_file(Parser *p, const char* filename);

void* parser_alloc(Parser *p, size_t size);
char* parser_strdup(Parser *p, const char *str);

void register_macro(Parser *p, const char *name, char **params, int param_count, Token *body, int body_len);
void register_typename(Parser *p, const char *name, int is_enum);
int is_typename(Parser *p, const char *name);
void register_alias(Parser *p, const char *name, VarType target);
VarType* get_alias(Parser *p, const char *name);

Token lexer_next_raw(Parser *p); 

// Expressions (parser/expr.c)
ASTNode* parse_call(Parser *p, char *name);
ASTNode* parse_postfix(Parser *p, ASTNode *node); 
ASTNode* parse_expression(Parser *p);

#include "modif.h"
#include "stmt.h"
#include "top.h"

#endif // PARSER_INTERNAL_H
