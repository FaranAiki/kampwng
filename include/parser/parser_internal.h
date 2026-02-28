#ifndef PARSER_INTERNAL_H
#define PARSER_INTERNAL_H

#include "parser.h"
#include "../common/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

// --- MODIFIER DEFINITIONS ---
#define MODIFIER_PUBLIC    (1 << 0)
#define MODIFIER_PRIVATE   (1 << 1)
#define MODIFIER_OPEN      (1 << 2)
#define MODIFIER_CLOSED    (1 << 3)
#define MODIFIER_CONST     (1 << 4)
#define MODIFIER_FINAL     (1 << 5)
#define MODIFIER_INERT     (1 << 6)
#define MODIFIER_REACTIVE  (1 << 7)
#define MODIFIER_NAKED     (1 << 8)
#define MODIFIER_STATIC    (1 << 9)
#define MODIFIER_PURE      (1 << 10)
#define MODIFIER_IMPURE    (1 << 11)
#define MODIFIER_PRISTINE  (1 << 12)
#define MODIFIER_TAINTED   (1 << 13)

int parse_modifiers(Parser* p);
void apply_class_modifiers(ClassNode* node, int modifiers);
void apply_func_modifiers(FuncDefNode* node, int modifiers);
void apply_var_modifiers(VarDeclNode* node, int modifiers);

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
#include "semantic.h"

#endif // PARSER_INTERNAL_H
