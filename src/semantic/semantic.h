#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "../parser/parser.h"

// ==========================================
// PART 1: SYMBOL TABLE (For Scoping)
// Maps "Name" -> "Symbol Info"
// ==========================================

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
    
    // Var specific
    int is_mutable;
    
    // Scope linkage
    struct SemScope *inner_scope; 
    
    struct SemSymbol *next; // Linked list bucket
} SemSymbol;

typedef struct SemScope {
    SemSymbol *symbols;
    struct SemScope *parent;
    int is_function_scope; 
    VarType expected_ret_type; 
} SemScope;

// ==========================================
// PART 2: SIDE TABLE (For Expressions)
// Maps "ASTNode Address" -> "Type"
// ==========================================

#define TYPE_TABLE_SIZE 1024

typedef struct TypeEntry {
    ASTNode *node;         // KEY: The pointer to the AST node
    VarType type;          // VALUE: The resolved type
    struct TypeEntry *next;
} TypeEntry;

// ==========================================
// PART 3: THE CONTEXT
// ==========================================

typedef struct {
    // 1. Symbol Table State (Scopes)
    SemScope *current_scope;
    SemScope *global_scope;
    
    // 2. Side Table State (Expression Types)
    TypeEntry *type_buckets[TYPE_TABLE_SIZE];

    // Error tracking
    int error_count;
    const char *current_source; 
    
    // Contextual flags
    int in_loop;
    int in_switch;
} SemanticCtx;

// --- API ---

// Lifecycle
void sem_init(SemanticCtx *ctx);
void sem_cleanup(SemanticCtx *ctx);

// Analysis Entry Point
int sem_check_program(SemanticCtx *ctx, ASTNode *root);

// Symbol Table Operations
void sem_scope_enter(SemanticCtx *ctx, int is_func, VarType ret_type);
void sem_scope_exit(SemanticCtx *ctx);
SemSymbol* sem_symbol_add(SemanticCtx *ctx, const char *name, SymbolKind kind, VarType type);
SemSymbol* sem_symbol_lookup(SemanticCtx *ctx, const char *name);

// Side Table Operations
void sem_set_node_type(SemanticCtx *ctx, ASTNode *node, VarType type);
VarType sem_get_node_type(SemanticCtx *ctx, ASTNode *node);

// Helpers
int sem_types_are_compatible(VarType dest, VarType src);
char* sem_type_to_str(VarType t);

void sem_check_node(SemanticCtx *ctx, ASTNode *node);
void sem_check_block(SemanticCtx *ctx, ASTNode *block);
void sem_check_expr(SemanticCtx *ctx, ASTNode *node);
void sem_scan_top_level(SemanticCtx *ctx, ASTNode *node);

#endif // SEMANTIC_H
