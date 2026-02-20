#ifndef PARSER_H
#define PARSER_H

#include "../lexer/lexer.h"
#include "../common/debug.h"
#include "../common/context.h"
#include <setjmp.h>
#include <stdbool.h>

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
  NODE_FOR_IN,
  NODE_WASH, // Added for wash/clean/untaint error handling blocks
  NODE_CLEAN // Added for wash/clean/untaint error handling blocks
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
  TYPE_ARRAY, // TODO not resolved
  TYPE_STRING,
  TYPE_VECTOR, // TODO not resolved
  TYPE_HASHMAP, // TODO not resolved
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

typedef struct VarType {
  BaseType base;
  int ptr_depth; 
  char *class_name;
  int array_size; 
  
  struct VarType *fp_ret_type;   
  struct VarType *fp_param_types; 
  int fp_param_count;

  // Packed bitfields
  bool is_unsigned : 1; 
  bool is_func_ptr : 1;
  bool fp_is_varargs : 1;
} VarType;

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
  char *class_name; 
  
  IsASemantic is_is_a;
  HasASemantic is_has_a;

  // Modifier Bitfields (Packed)
  bool is_varargs : 1; 
  bool is_public : 1;
  bool is_open : 1;
  bool is_static : 1;
  bool is_virtual : 1;
  bool is_abstract : 1;
  bool is_flux : 1;
  bool is_pure : 1;
  bool is_pristine : 1;
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
  
  IsASemantic is_is_a;
  HasASemantic is_has_a;

  // Modifier Bitfields (Packed)
  bool is_open : 1; 
  bool is_public : 1; 
  bool is_extern : 1; 
  bool is_union : 1;
  bool is_static : 1; 
  bool is_abstract : 1;
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
  bool is_static : 1;      
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
    bool is_leak : 1;    
} CaseNode;

typedef struct {
    ASTNode base;
    ASTNode *condition;
    ASTNode *cases; 
    ASTNode *default_case; 
} SwitchNode;

// Added for Wash/Clean/Untaint error handling
typedef struct {
    ASTNode base;
    ASTNode *expr;
    char *err_name;
    ASTNode *body;
    ASTNode *else_body;
    unsigned int wash_type : 2; // 0=wash, 1=clean, 2=untaint
} WashNode;

typedef struct {
  ASTNode base;
  VarType var_type;
  char *name;
  ASTNode *initializer;
  ASTNode *array_size; 

  IsASemantic is_is_a;
  HasASemantic is_has_a;

  // Modifier Bitfields (Packed)
  bool is_array : 1;   
  bool is_open : 1;
  bool is_public : 1;
  bool is_static : 1;
  bool is_const : 1;
  bool is_mutable : 1; 
  bool is_pure : 1;
  bool is_pristine : 1;
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
  ASTNode *target; 
  int op;
  bool is_prefix : 1; 
} IncDecNode;

typedef struct {
  ASTNode base;
  char *name;
  char *mangled_name;
  bool is_class_member : 1; 
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
  bool is_do_while : 1; 
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
#include "modifier.h"

#endif // PARSER_H
