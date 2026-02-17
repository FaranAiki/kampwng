#ifndef SEMANTIC_CHECKED_H
#define SEMANTIC_CHECKED_H

#include "../parser/parser.h"

// Enum for Checked Nodes
// These mirror the parser nodes but imply semantic validation has occurred
typedef enum {
    CHECKED_ROOT,
    CHECKED_FUNC_DEF,
    CHECKED_BLOCK,
    CHECKED_RETURN,
    CHECKED_BREAK,
    CHECKED_CONTINUE,
    CHECKED_LOOP,
    CHECKED_WHILE,
    CHECKED_IF,
    CHECKED_SWITCH,
    CHECKED_CASE,
    CHECKED_VAR_DECL,
    CHECKED_ASSIGN,
    CHECKED_BINARY_OP,
    CHECKED_UNARY_OP,
    CHECKED_LITERAL,
    CHECKED_VAR_REF,
    CHECKED_CALL,
    CHECKED_ARRAY_ACCESS,
    CHECKED_MEMBER_ACCESS,
    CHECKED_METHOD_CALL,
    CHECKED_CAST,           // Explicit node for casts (both source-level and implicit)
    CHECKED_TRAIT_ACCESS,
    CHECKED_CLASS,
    CHECKED_NAMESPACE,
    CHECKED_ENUM,
    // Flux / Generator
    CHECKED_EMIT,
    CHECKED_FOR_IN
} CheckedNodeType;

// Base Checked AST Node
// Key Difference: 'type' is fully resolved. No TYPE_AUTO or TYPE_UNKNOWN allowed in expressions.
typedef struct ASTCheckedNode {
    CheckedNodeType node_type;
    struct ASTCheckedNode *next;
    
    // Source Location (preserved for error reporting in later stages)
    int line;
    int col;
    
    // The resolved semantic type of this expression.
    // For statements (if, while), this is typically TYPE_VOID.
    VarType type; 
} ASTCheckedNode;

// Parameter with resolved type
typedef struct CheckedParameter {
    VarType type;
    char *name;
    struct CheckedParameter *next;
} CheckedParameter;

// Function Definition (Resolved)
typedef struct {
    ASTCheckedNode base;
    char *name;
    char *mangled_name; // Fully resolved name (e.g., _Z3fooi)
    VarType ret_type;
    CheckedParameter *params;
    ASTCheckedNode *body;
    int is_varargs;
    int is_flux;
    char *owner_class;  // If method, the class name
} CheckedFuncDefNode;

// Variable Declaration (Inferred)
// 'auto'/'let' is replaced by the concrete type of the initializer.
typedef struct {
    ASTCheckedNode base;
    VarType var_type;   // Concrete type (e.g., TYPE_INT)
    char *name;
    ASTCheckedNode *initializer; // Can be NULL
    int is_mutable;
    int is_array;
    int array_size;     // Resolved integer size
} CheckedVarDeclNode;

// Assignment
typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *target; // l-value (CheckedVarRef, CheckedMemberAccess, etc.)
    ASTCheckedNode *value;  // r-value (Expression)
    int op;                 // Token type (TOKEN_ASSIGN, TOKEN_PLUS_ASSIGN, etc.)
} CheckedAssignNode;

// Binary Operation
// Implicit casts are inserted as children if needed.
typedef struct {
    ASTCheckedNode base;
    int op;
    ASTCheckedNode *left;
    ASTCheckedNode *right;
} CheckedBinaryOpNode;

// Unary Operation
typedef struct {
    ASTCheckedNode base;
    int op;
    ASTCheckedNode *operand;
} CheckedUnaryOpNode;

// Literal (Typed)
typedef struct {
    ASTCheckedNode base;
    union {
        int int_val;
        unsigned long long long_val;
        double double_val;
        char *str_val;
    } val;
} CheckedLiteralNode;

// Variable Reference (Resolved)
typedef struct {
    ASTCheckedNode base;
    char *name;
    char *mangled_name; // If referring to a function pointer or global
    // Could add: symbol table index or pointer
} CheckedVarRefNode;

// Function Call (Resolved)
typedef struct {
    ASTCheckedNode base;
    char *name;
    char *mangled_name; // The specific overload selected
    ASTCheckedNode *args;
} CheckedCallNode;

// Cast (Explicit or Implicit)
// Used when the semantic analyzer detects a type mismatch that can be coerced
// or when the user uses 'as'.
typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *operand;
    VarType target_type; // The type we are casting TO
} CheckedCastNode;

// Member Access
typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *object;
    char *member_name;
    int member_index;    // Optimization: Index in the struct
} CheckedMemberAccessNode;

// Method Call
typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *object;
    char *method_name;
    char *mangled_name;  // Resolved method name
    ASTCheckedNode *args;
} CheckedMethodCallNode;

// Array Access
typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *target;
    ASTCheckedNode *index;
} CheckedArrayAccessNode;

// Control Flow
typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *condition;
    ASTCheckedNode *then_body;
    ASTCheckedNode *else_body;
} CheckedIfNode;

typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *condition;
    ASTCheckedNode *body;
    int is_do_while;
} CheckedWhileNode;

typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *iterations;
    ASTCheckedNode *body;
} CheckedLoopNode;

typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *value; // Return value (or NULL)
} CheckedReturnNode;

typedef struct {
    ASTCheckedNode base;
} CheckedBreakNode;

typedef struct {
    ASTCheckedNode base;
} CheckedContinueNode;

// Switch
typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *value; // Constant value
    ASTCheckedNode *body;
    int is_leak;
} CheckedCaseNode;

typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *condition;
    ASTCheckedNode *cases;
    ASTCheckedNode *default_case;
} CheckedSwitchNode;

// Classes (Resolved)
// Members here are fully checked.
typedef struct {
    ASTCheckedNode base;
    char *name;
    char *parent_name;
    ASTCheckedNode *members;
    int is_extern;
    int is_union;
} CheckedClassNode;

typedef struct {
    ASTCheckedNode base;
    char *name;
    ASTCheckedNode *body;
} CheckedNamespaceNode;

typedef struct {
    char *name;
    int value;
    struct CheckedEnumEntry *next;
} CheckedEnumEntry;

typedef struct {
    ASTCheckedNode base;
    char *name;
    CheckedEnumEntry *entries;
} CheckedEnumNode;

// Flux / Generators
typedef struct {
    ASTCheckedNode base;
    ASTCheckedNode *value;
} CheckedEmitNode;

typedef struct {
    ASTCheckedNode base;
    char *var_name;
    ASTCheckedNode *collection;
    ASTCheckedNode *body;
    VarType iter_type; // Explicitly resolved type of the iterator
} CheckedForInNode;

#endif // SEMANTIC_CHECKED_H
