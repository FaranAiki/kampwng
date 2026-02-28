#ifndef SEMANTIC_TYPESTRUCT_H
#define SEMANTIC_TYPESTRUCT_H

#include "parser/typestruct.h"

typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_CLASS,
    SYM_ENUM,
    SYM_NAMESPACE
} SymbolKind;

typedef struct SemSymbol {
    char *name;
    SymbolKind kind;
    VarType type;         // For VAR, FUNC (return type)
    
    // Function specific
    VarType *param_types;
    int param_count;
    
    // Class specific
    char *parent_name;    // For inheritance lookup
    
    // Semantic Modifiers
    IsASemantic is_is_a;
    HasASemantic is_has_a;
    
    // Scope linkage
    struct SemScope *inner_scope; 
    struct SemSymbol *next; // Linked list bucket
    
    // Packed bitfields
    bool is_mutable : 1;
    bool is_initialized : 1;   // Track if variable has been assigned a value
    bool is_used_as_parent : 1;
    bool is_used_as_composition : 1;
    bool is_pure : 1;
    bool is_pristine : 1;
    bool is_flux : 1;          // Marks a function as a flux generator 
} SemSymbol;

typedef struct SemScope {
    SemSymbol *symbols;
    struct SemScope *parent;
    SemSymbol *class_sym;  // Pointer to the Class Symbol this scope belongs to (for inheritance)
    VarType expected_ret_type; 

    // Packed bitfields
    bool is_function_scope : 1; 
    bool is_class_scope : 1;    // Identifies if this scope belongs to a class
} SemScope;

// TODO use hashmap for this

#define TYPE_TABLE_SIZE 1024

typedef struct TypeEntry {
    ASTNode *node;         // KEY: The pointer to the AST node
    VarType type;          // VALUE: The resolved type
    int is_tainted;        // VALUE: The evaluated taint status
    int is_impure;        // VALUE: The evaluated impure status
    struct TypeEntry *next;
} TypeEntry;

typedef struct {
    CompilerContext *compiler_ctx;

    SemScope *current_scope;
    SemScope *global_scope;
    
    SemSymbol *current_func_sym;
    
    int in_wash_block;
    
    TypeEntry *type_buckets[TYPE_TABLE_SIZE];

    const char *current_source; 
    const char *current_filename; 
    
    int in_loop;
    int in_switch;
} SemanticCtx;

#endif // SEMANTIC_TYPESTRUCT_H
