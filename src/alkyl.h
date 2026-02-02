#ifndef KAMPWNG_H
#define KAMPWNG_H

#include <llvm-c/Core.h>
#include <stdbool.h>

// --- LEXER TYPES ---

typedef enum {
  TOKEN_EOF,
  TOKEN_LOOP,   // loop
  TOKEN_PRINT,  // print
  TOKEN_LBRACKET, // [
  TOKEN_RBRACKET, // ]
  TOKEN_LBRACE,   // {
  TOKEN_RBRACE,   // }
  TOKEN_SEMICOLON,// ;
  TOKEN_NUMBER,   // 10, 42
  TOKEN_STRING,   // "Hello"
  TOKEN_UNKNOWN
} TokenType;

typedef struct {
  TokenType type;
  char *text;   // Stores the identifier name or string content
  int int_val;  // Stores integer value if type is NUMBER
} Token;

typedef struct {
  const char *src;
  int pos;
} Lexer;

// --- AST TYPES (Abstract Syntax Tree) ---

typedef enum {
  NODE_ROOT,
  NODE_LOOP,
  NODE_PRINT
} NodeType;

// Base struct for polymorphism simulation
typedef struct ASTNode {
  NodeType type;
  struct ASTNode *next; // Linked list for sequences of statements
} ASTNode;

typedef struct {
  ASTNode base;
  int iterations;
  ASTNode *body; // The code inside the loop
} LoopNode;

typedef struct {
  ASTNode base;
  char *message;
} PrintNode;

// --- FUNCTIONS ---

// Lexer
void lexer_init(Lexer *l, const char *src);
Token lexer_next(Lexer *l);
void free_token(Token t);

// Parser
ASTNode* parse_program(Lexer *l);
void free_ast(ASTNode *node);

// Codegen
LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name);

#endif
