#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include <setjmp.h>

// --- TYPES ---

typedef enum {
  NODE_ROOT,
  NODE_FUNC_DEF,  
  NODE_CALL,    
  NODE_RETURN,  
  NODE_BREAK,
  NODE_CONTINUE,
  NODE_LOOP,    
  NODE_WHILE,   
  NODE_IF,
  NODE_VAR_DECL,  
  NODE_ASSIGN,  
  NODE_VAR_REF,
  NODE_BINARY_OP,
  NODE_UNARY_OP, 
  NODE_LITERAL,
  NODE_ARRAY_LIT, 
  NODE_ARRAY_ACCESS, 
  NODE_INC_DEC, 
  NODE_LINK 
} NodeType;

typedef enum {
  TYPE_VOID,
  TYPE_INT,
  TYPE_CHAR,
  TYPE_BOOL,
  TYPE_FLOAT,
  TYPE_DOUBLE,
  TYPE_STRING,
  TYPE_AUTO,
  TYPE_UNKNOWN
} BaseType;

typedef struct {
  BaseType base;
  int ptr_depth; // 0 = val, 1 = *val, 2 = **val
} VarType;

typedef struct ASTNode {
  NodeType type;
  struct ASTNode *next; 
} ASTNode;

typedef struct Parameter {
  VarType type;
  char *name; // Can be NULL for extern
  struct Parameter *next;
} Parameter;

typedef struct {
  ASTNode base;
  char *name;
  VarType ret_type;
  Parameter *params;
  ASTNode *body; // NULL if extern (FFI)
  int is_varargs; 
} FuncDefNode;

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *args; 
} CallNode;

typedef struct {
  ASTNode base;
  ASTNode *value;
} ReturnNode;

typedef struct {
  ASTNode base;
} BreakNode;

typedef struct {
  ASTNode base;
} ContinueNode;

typedef struct {
  ASTNode base;
  ASTNode *iterations;
  ASTNode *body;
} LoopNode;

typedef struct {
  ASTNode base;
  ASTNode *condition;
  ASTNode *body;
  int is_do_while; 
} WhileNode;

typedef struct {
  ASTNode base;
  ASTNode *condition;
  ASTNode *then_body;
  ASTNode *else_body;
} IfNode;

typedef struct {
  ASTNode base;
  VarType var_type;
  char *name;
  ASTNode *initializer;
  int is_mutable; 
  int is_array;   
  ASTNode *array_size; 
} VarDeclNode;

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *value;
  ASTNode *index; 
  int op; 
} AssignNode; 

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *index; 
  int is_prefix; 
  int op; 
} IncDecNode;

typedef struct {
  ASTNode base;
  char *name;
} VarRefNode;

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *index;
} ArrayAccessNode;

typedef struct {
  ASTNode base;
  ASTNode *elements; 
} ArrayLitNode;

typedef struct {
  ASTNode base;
  char *lib_name;
} LinkNode;

typedef struct {
  ASTNode base;
  int op; 
  ASTNode *left;
  ASTNode *right;
} BinaryOpNode;

typedef struct {
  ASTNode base;
  int op; 
  ASTNode *operand;
} UnaryOpNode;

typedef struct {
  ASTNode base;
  VarType var_type;
  union {
    int int_val;
    double double_val;
    char *str_val; 
  } val;
} LiteralNode;

// --- PROTOTYPES ---

ASTNode* parse_program(Lexer *l);
ASTNode* parse_expression(Lexer *l);
void free_ast(ASTNode *node);
void safe_free_current_token(void);

// Reset internal parser state (crucial for REPL)
void parser_reset(void);

// CLI Hooks
void parser_set_recovery(jmp_buf *env);

#endif
