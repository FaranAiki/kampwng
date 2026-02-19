#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "../parser/parser.h"
#include "../common/common.h"
#include "../common/diagnostic.h"
#include "../common/context.h"

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
    
    // Class specific
    char *parent_name;    // For inheritance lookup

    // Var specific
    int is_mutable;
    int is_initialized;   // Track if variable has been assigned a value
    
    // Scope linkage
    struct SemScope *inner_scope; 
    
    struct SemSymbol *next; // Linked list bucket
} SemSymbol;

typedef struct SemScope {
    SemSymbol *symbols;
    struct SemScope *parent;
    int is_function_scope; 
    int is_class_scope;    // Identifies if this scope belongs to a class
    SemSymbol *class_sym;  // Pointer to the Class Symbol this scope belongs to (for inheritance)
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
    // Shared Compiler Context (Arena, Diagnostics)
    CompilerContext *compiler_ctx;

    // 1. Symbol Table State (Scopes)
    SemScope *current_scope;
    SemScope *global_scope;
    
    // 2. Side Table State (Expression Types)
    TypeEntry *type_buckets[TYPE_TABLE_SIZE];

    // Contextual information for error reporting
    const char *current_source; 
    const char *current_filename; 
    
    // Contextual flags
    int in_loop;
    int in_switch;
} SemanticCtx;

// Lifecycle
void sem_init(SemanticCtx *ctx, CompilerContext *compiler_ctx);
// sem_cleanup is largely unnecessary with Arena, but kept for consistency/resetting non-arena state
void sem_cleanup(SemanticCtx *ctx);

// Analysis Entry Point
int sem_check_program(SemanticCtx *ctx, ASTNode *root);

// Symbol Table Operations
void sem_scope_enter(SemanticCtx *ctx, int is_func, VarType ret_type);
void sem_scope_exit(SemanticCtx *ctx);
SemSymbol* sem_symbol_add(SemanticCtx *ctx, const char *name, SymbolKind kind, VarType type);

// Lookup: optionally returns the scope where the symbol was found
SemSymbol* sem_symbol_lookup(SemanticCtx *ctx, const char *name, SemScope **out_scope);

// Side Table Operations
void sem_set_node_type(SemanticCtx *ctx, ASTNode *node, VarType type);
VarType sem_get_node_type(SemanticCtx *ctx, ASTNode *node);

// Helpers
int sem_types_are_compatible(VarType dest, VarType src);
char* sem_type_to_str(VarType t);

// Reporting
void sem_error(SemanticCtx *ctx, ASTNode *node, const char *fmt, ...);
void sem_info(SemanticCtx *ctx, ASTNode *node, const char *fmt, ...);
void sem_hint(SemanticCtx *ctx, ASTNode *node, const char *fmt, ...);

void sem_register_builtins(SemanticCtx *ctx);

void sem_check_node(SemanticCtx *ctx, ASTNode *node);
void sem_check_block(SemanticCtx *ctx, ASTNode *block);
void sem_check_expr(SemanticCtx *ctx, ASTNode *node);
void sem_scan_top_level(SemanticCtx *ctx, ASTNode *node);

SemSymbol* lookup_local_symbol(SemanticCtx *ctx, const char *name);

void sem_check_func_def(SemanticCtx *ctx, FuncDefNode *node);


#include "emitter.h"
#include "type.h"

#endif // SEMANTIC_H
