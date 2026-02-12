#ifndef PARSER_H
#define PARSER_H

#include "../lexer/lexer.h"
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
  NODE_SWITCH,
  NODE_CASE,
  NODE_VAR_DECL,  
  NODE_ASSIGN,  
  NODE_VAR_REF,
  NODE_BINARY_OP,
  NODE_UNARY_OP, 
  NODE_LITERAL,
  NODE_ARRAY_LIT, 
  NODE_ARRAY_ACCESS, 
  NODE_INC_DEC, 
  NODE_LINK,
  NODE_CLASS,
  NODE_NAMESPACE, 
  NODE_ENUM, 
  NODE_MEMBER_ACCESS,
  NODE_METHOD_CALL, 
  NODE_TRAIT_ACCESS, 
  NODE_TYPEOF,
  NODE_HAS_METHOD,    
  NODE_HAS_ATTRIBUTE,  
  NODE_CAST,
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
  TYPE_CLASS, 
  TYPE_UNKNOWN
} BaseType;

typedef struct {
  BaseType base;
  int ptr_depth; 
  char *class_name;
  int array_size; 
} VarType;

typedef struct ASTNode {
  NodeType type;
  struct ASTNode *next; 
  // Source Location
  int line;
  int col;
} ASTNode;

typedef struct Parameter {
  VarType type;
  char *name; 
  struct Parameter *next;
} Parameter;

typedef struct {
  ASTNode base;
  char *name;
  char *mangled_name; // Added for overloading
  VarType ret_type;
  Parameter *params;
  ASTNode *body; 
  int is_varargs; 
  int is_open; 
  char *class_name; 
} FuncDefNode;

typedef struct {
  ASTNode base;
  char *name;
  char *parent_name; 
  struct {
      char **names;
      int count;
  } traits; 
  ASTNode *members; 
  int is_open; 
} ClassNode;

typedef struct EnumEntry {
    char *name;
    int value;
    struct EnumEntry *next;
} EnumEntry;

typedef struct {
    ASTNode base;
    char *name;
    EnumEntry *entries;
} EnumNode;

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *body; 
} NamespaceNode;

typedef struct {
  ASTNode base;
  ASTNode *object; 
  char *member_name; 
} MemberAccessNode;

typedef struct {
  ASTNode base;
  ASTNode *object;
  char *method_name;
  ASTNode *args;
  // Resolution Info
  char *mangled_name; // The actual function to call (e.g., _Z7Parent3foo...)
  char *owner_class;  // The class/trait where it was found (for 'this' adjustment)
  int is_static;      // Added for namespace calls
} MethodCallNode;

typedef struct {
  ASTNode base;
  ASTNode *object;
  char *trait_name;
} TraitAccessNode;

typedef struct {
    ASTNode base;
    VarType var_type;
    ASTNode *operand;
} CastNode;

typedef struct {
  ASTNode base;
  char *name;
  char *mangled_name; // Added: resolved overload name
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

// Switch Structs
typedef struct {
    ASTNode base;
    ASTNode *value; // Case Value (Literal/Enum)
    ASTNode *body;  // Statement List
    int is_leak;    // Fallthrough flag
} CaseNode;

typedef struct {
    ASTNode base;
    ASTNode *condition;
    ASTNode *cases; // List of CaseNodes
    ASTNode *default_case; // List of statements for default
} SwitchNode;

typedef struct {
  ASTNode base;
  VarType var_type;
  char *name;
  ASTNode *initializer;
  int is_mutable; 
  int is_array;   
  ASTNode *array_size; 
  int is_open; 
} VarDeclNode;

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *value;
  ASTNode *index; 
  ASTNode *target; 
  int op; 
} AssignNode; 

typedef struct {
  ASTNode base;
  char *name;
  ASTNode *index; 
  int is_prefix; 
  int op;
  ASTNode *target; 
} IncDecNode;

typedef struct {
  ASTNode base;
  char *name;
} VarRefNode;

typedef struct {
  ASTNode base;
  ASTNode *target; 
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

ASTNode* parse_program(Lexer *l);
ASTNode* parse_expression(Lexer *l);
void free_ast(ASTNode *node);
void safe_free_current_token(void);

// Reset internal parser state (crucial for REPL)
void parser_reset(void);

// CLI Hooks
void parser_set_recovery(jmp_buf *env);

#endif
