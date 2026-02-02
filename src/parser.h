#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

// --- TYPES ---

typedef enum {
    NODE_ROOT,
    NODE_FUNC_DEF,  // type name(args) { body }
    NODE_CALL,      // name(args)
    NODE_RETURN,    // return x;
    NODE_LOOP,
    NODE_IF,
    NODE_VAR_DECL,  
    NODE_ASSIGN,    
    NODE_VAR_REF,
    NODE_BINARY_OP,
    NODE_UNARY_OP, 
    NODE_LITERAL
} NodeType;

typedef enum {
    VAR_VOID,
    VAR_INT,
    VAR_CHAR,
    VAR_BOOL,
    VAR_FLOAT,
    VAR_DOUBLE,
    VAR_STRING // Added for proper string handling
} VarType;

typedef struct ASTNode {
    NodeType type;
    struct ASTNode *next; 
} ASTNode;

typedef struct Parameter {
    VarType type;
    char *name;
    struct Parameter *next;
} Parameter;

typedef struct {
    ASTNode base;
    char *name;
    VarType ret_type;
    Parameter *params;
    ASTNode *body;
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
    ASTNode *iterations;
    ASTNode *body;
} LoopNode;

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
} VarDeclNode;

typedef struct {
    ASTNode base;
    char *name;
    ASTNode *value;
} AssignNode; 

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
        char *str_val; // Added for strings
    } val;
} LiteralNode;

// --- PROTOTYPES ---

ASTNode* parse_program(Lexer *l);
void free_ast(ASTNode *node);

#endif
