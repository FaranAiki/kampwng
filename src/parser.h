#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

// --- TYPES ---

typedef enum {
  NODE_ROOT,
  NODE_LOOP,
  NODE_IF,    // New: if/elif/else
  NODE_PRINT,
  NODE_VAR_DECL,
  NODE_VAR_REF,
  NODE_BINARY_OP,
  NODE_LITERAL
} NodeType;

typedef enum {
  VAR_VOID,
  VAR_INT,
  VAR_CHAR,
  VAR_BOOL,
  VAR_FLOAT,
  VAR_DOUBLE
} VarType;

typedef struct ASTNode {
  NodeType type;
  struct ASTNode *next; 
} ASTNode;

typedef struct {
  ASTNode base;
  ASTNode *iterations;
  ASTNode *body;
} LoopNode;

typedef struct {
  ASTNode base;
  ASTNode *condition;
  ASTNode *then_body;
  ASTNode *else_body; // Can be NULL, another IF node (for elif), or a statement list
} IfNode;

typedef struct {
  ASTNode base;
  char *message;
} PrintNode;

typedef struct {
  ASTNode base;
  VarType var_type;
  char *name;
  ASTNode *initializer;
} VarDeclNode;

typedef struct {
  ASTNode base;
  char *name;
} VarRefNode;

typedef struct {
  ASTNode base;
  int op; 
  ASTNode *left;
  ASTNode *right;
} BinaryOpNode;

typedef struct {
  ASTNode base;
  VarType var_type;
  union {
    int int_val;
    double double_val;
  } val;
} LiteralNode;

// --- PROTOTYPES ---

ASTNode* parse_program(Lexer *l);
void free_ast(ASTNode *node);

#endif
