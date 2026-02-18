#ifndef PARSER_H
#define PARSER_H

#include "../lexer/lexer.h"
#include "../common/debug.h"
#include "../common/context.h"
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
  NODE_EMIT,
  NODE_FOR_IN
} NodeType;

typedef enum {
  TYPE_VOID,
  TYPE_INT,
  TYPE_SHORT,
  TYPE_LONG,
  TYPE_LONG_LONG,
  TYPE_CHAR,
  TYPE_BOOL,
  TYPE_FLOAT,
  TYPE_DOUBLE,
  TYPE_LONG_DOUBLE,
  TYPE_STRING,
  TYPE_AUTO,
  TYPE_CLASS, 
  TYPE_ENUM, 
  TYPE_UNKNOWN
} BaseType;

typedef enum {
  IS_A_NONE,
  IS_A_NAKED, // MUST be inherited, e.g. if there is no class/function inheriting/replacing it, throw semantic error
  IS_A_FINAL, // CANNOT be inherited like in Java, e.g. final cannot be override
} IsASemantic; // inheritance

typedef enum {
  HAS_A_NONE,
  HAS_A_REACTIVE, // MUST be composed
  HAS_A_INERT, // CANNOT be composed
} HasASemantic; // composition

typedef struct VarType VarType;
struct VarType {
  BaseType base;
  int ptr_depth; 
  char *class_name;
  int array_size; 
  int is_unsigned; 
  
  int is_func_ptr;
  struct VarType *fp_ret_type;   
  struct VarType *fp_param_types; 
  int fp_param_count;
  int fp_is_varargs;
};

typedef struct ASTNode {
  NodeType type;
  struct ASTNode *next; 
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
  char *mangled_name; 
  VarType ret_type;
  Parameter *params;
  ASTNode *body; 
  int is_varargs; 
  int is_public; // public can be exposed, if false private
  int is_open; // open can be extensible, if false closed
  int is_static;
  int is_virtual; // virtual keyword 
  int is_abstract; // this is like Java's abstract keyword
  int is_is_a; // use the IsASemantic
  int is_has_a; // use the HasASemantic
  char *class_name; 
  int is_flux;
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
  int is_public; 
  int is_extern; 
  int is_union;
  int is_static; 
  int is_abstract; // like in C++, C#, Java
  int is_is_a; // use the IsASemantic
  int is_has_a; // use the HasASemantic
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
  char *mangled_name; 
  char *owner_class;  
  int is_static;      
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
  char *mangled_name; 
  ASTNode *args; 
} CallNode;

typedef struct {
  ASTNode base;
  ASTNode *value;
} ReturnNode;

typedef struct {
    ASTNode base;
    ASTNode *value; 
    ASTNode *body;  
    int is_leak;    
} CaseNode;

typedef struct {
    ASTNode base;
    ASTNode *condition;
    ASTNode *cases; 
    ASTNode *default_case; 
} SwitchNode;

typedef struct {
  ASTNode base;
  VarType var_type;
  char *name;
  ASTNode *initializer;
  int is_array;   
  ASTNode *array_size; 
  int is_open;
  int is_public;
  int is_static;
  int is_const;
  int is_mutable; 
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
  char *mangled_name;
  int is_class_member; 
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
    ASTNode *value;
} EmitNode;

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
    char *var_name;
    ASTNode *collection;
    ASTNode *body;
    VarType iter_type; 
} ForInNode;

typedef struct {
  ASTNode base;
  ASTNode *condition;
  ASTNode *then_body;
  ASTNode *else_body;
} IfNode;

typedef struct {
  ASTNode base;
  VarType var_type;
  union {
    int int_val;
    unsigned long long long_val;
    double double_val;
    char *str_val; 
  } val;
} LiteralNode;

// --- PARSER CONTEXT ---

// Forward declarations for internal state structures
typedef struct Macro Macro;
typedef struct TypeName TypeName;
typedef struct TypeAlias TypeAlias;
typedef struct Expansion Expansion;

typedef struct Parser {
    Lexer *l;
    CompilerContext *ctx;
    
    Token current_token;
    
    // Error Recovery
    jmp_buf *recover_buf;
    
    // Internal State (Lists)
    Macro *macro_head;
    TypeName *type_head;
    TypeAlias *alias_head;
    Expansion *expansion_head;
    
} Parser;

// Initialize parser with an existing lexer (which holds the context)
void parser_init(Parser *p, Lexer *l);

// Parse the program using the provided parser instance
ASTNode* parse_program(Parser *p);
ASTNode* parse_expression(Parser *p);

#include "emitter.h"
#include "link.h"

#endif // PARSER_H
