#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include <setjmp.h>

// --- TYPES ---

typedef enum {
  NODE_ROOT,
  NODE_FUNC_DEF,  // type name(args) { body }
  NODE_CALL,    // name(args)
  NODE_RETURN,  // return x;
  NODE_BREAK,
  NODE_CONTINUE,
  NODE_LOOP,    // loop N { ... }
  NODE_WHILE,   // while (once) cond { ... }
  NODE_IF,
  NODE_VAR_DECL,  
  NODE_ASSIGN,  
  NODE_VAR_REF,
  NODE_BINARY_OP,
  NODE_UNARY_OP, 
  NODE_LITERAL,
  NODE_ARRAY_LIT, // [1, 2, 3]
  NODE_ARRAY_ACCESS, // t[0]
  NODE_INC_DEC, // ++x, x--
  NODE_LINK // link m
} NodeType;

typedef enum {
  VAR_VOID,
  VAR_INT,
  VAR_CHAR,
  VAR_BOOL,
  VAR_FLOAT,
  VAR_DOUBLE,
  VAR_STRING,
  VAR_AUTO // For 'let' type inference
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
  int is_varargs; // 1 if ... matches at end of params
} FuncDefNode;

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *args; // Linked list of expression nodes
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
  int is_do_while; // 1 if "while once" (do-while), 0 if "while"
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
  int is_mutable; // 1 = mut, 0 = imut
  int is_array;   // 1 = yes
  ASTNode *array_size; // Expression for size, or NULL if inferred or not array
} VarDeclNode;

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *value;
  ASTNode *index; // NULL if normal assignment, not NULL if array[index] = value
  int op; // TOKEN_ASSIGN, TOKEN_PLUS_ASSIGN, etc.
} AssignNode; 

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *index; // NULL if variable, else array index
  int is_prefix; // 1 for ++x, 0 for x++
  int op; // TOKEN_INCREMENT or TOKEN_DECREMENT
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
  ASTNode *elements; // Linked list
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
  // Union for value storage
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
