#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "../parser/parser.h"
#include "../common/common.h"
#include "../common/diagnostic.h"
#include "../common/context.h"
#include <stdbool.h>

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

void sem_init(SemanticCtx *ctx, CompilerContext *compiler_ctx);
void sem_cleanup(SemanticCtx *ctx);

int sem_check_program(SemanticCtx *ctx, ASTNode *root);

void sem_scope_enter(SemanticCtx *ctx, int is_func, VarType ret_type);
void sem_scope_exit(SemanticCtx *ctx);
SemSymbol* sem_symbol_add(SemanticCtx *ctx, const char *name, SymbolKind kind, VarType type);

SemSymbol* sem_symbol_lookup(SemanticCtx *ctx, const char *name, SemScope **out_scope);

void sem_set_node_type(SemanticCtx *ctx, ASTNode *node, VarType type);
VarType sem_get_node_type(SemanticCtx *ctx, ASTNode *node);

void sem_set_node_tainted(SemanticCtx *ctx, ASTNode *node, int is_tainted);
void sem_set_node_impure(SemanticCtx *ctx, ASTNode *node, int is_impure);
int sem_get_node_tainted(SemanticCtx *ctx, ASTNode *node);
int sem_get_node_impure(SemanticCtx *ctx, ASTNode *node);

int sem_types_are_compatible(VarType dest, VarType src);
char* sem_type_to_str(VarType t);

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

void sem_check_var_decl(SemanticCtx *ctx, VarDeclNode *node, int register_sym);
void sem_check_stmt(SemanticCtx *ctx, ASTNode *node);
void sem_insert_implicit_cast(SemanticCtx *ctx, ASTNode **node_ptr, VarType target_type);

#include "emitter.h"
#include "type.h"
#include "fragment/lookup.h"
#include "fragment/switch.h"

#endif // SEMANTIC_H
