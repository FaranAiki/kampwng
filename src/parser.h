#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

typedef enum {
  NODE_ROOT,
  NODE_LOOP,
  NODE_PRINT,
  NODE_VAR_DECL,  // int x = 5;
  NODE_VAR_REF,   // x
  NODE_BINARY_OP, // x + 5
  NODE_LITERAL  // 10, 3.14
} NodeType;

typedef enum {
  VAR_VOID,
  VAR_INT,  // i32
  VAR_CHAR,   // i8
  VAR_BOOL,   // i1
  VAR_FLOAT,  // float (single)
  VAR_DOUBLE  // double
} VarType;

// Base struct for polymorphism
// TODO implement this
typedef struct ASTNode {
  NodeType type;
  struct ASTNode *next; 
} ASTNode;

// Expressions (Literals, Vars, Ops) do not have a 'next' usually, 
// but we inherit from ASTNode for simplicity in function signatures.

typedef struct {
  ASTNode base;
  ASTNode *iterations; // Now an expression, not just an int
  ASTNode *body;
} LoopNode;

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
  int op; // Token type (TOKEN_PLUS, etc)
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

ASTNode* parse_program(Lexer *l);
void free_ast(ASTNode *node);

#endif
