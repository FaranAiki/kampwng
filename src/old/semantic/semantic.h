#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "../diagnostic/diagnostic.h"
#include "../parser/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct SemSymbol {
    char *name;
    VarType type;
    int is_mutable;
    int is_array;
    int array_size; // 0 if unknown or dynamic
    // Location for error reporting
    int decl_line;
    int decl_col;
    struct SemSymbol *next;
} SemSymbol;

typedef struct SemFunc {
    char *name;          // Original name
    char *mangled_name;  // Mangled signature
    VarType ret_type;
    VarType *param_types;
    int param_count;
    int is_flux;         // Added for flux checking
    struct SemFunc *next;
} SemFunc;

typedef struct SemEnum {
    char *name;
    struct SemEnumMember { 
        char *name; 
        struct SemEnumMember *next; 
    } *members;
    struct SemEnum *next;
} SemEnum;

typedef struct SemClass {
    char *name;
    char *parent_name; 
    char **traits;      // List of implemented traits
    int trait_count;
    SemSymbol *members; // List of class fields
    int is_extern;      // Opaque tracking
    int is_union;       // Union tracking
    struct SemClass *next;
} SemClass;

typedef struct SemTypedef {
    char *name;
    VarType target_type;
    struct SemTypedef *next;
} SemTypedef;

typedef struct Scope {
    SemSymbol *symbols;
    struct Scope *parent;
} Scope;

typedef struct {
    Scope *current_scope;
    SemFunc *functions;
    SemClass *classes;
    SemEnum *enums; 
    SemTypedef *typedefs;
    
    int error_count;
    
    // Context tracking
    VarType current_func_ret_type; // For checking return types
    int in_loop;                   // For checking break/continue
    int in_flux;                   // Added: For checking emit
    const char *current_class;     // For 'this' context
    const char *source_code;       // For error reporting
    const char *filename;          // For error reporting context
} SemCtx;

// Recursive checkers
VarType check_expr(SemCtx *ctx, ASTNode *node);
void check_stmt(SemCtx *ctx, ASTNode *node);
void check_block(SemCtx *ctx, ASTNode *node);

// Mangling helpers
void mangle_type(char *buf, VarType t);
char* mangle_function(const char *name, Parameter *params);

// Diagnostic / Error Reporting helpers
void sem_error(SemCtx *ctx, ASTNode *node, const char *fmt, ...);
void sem_info(SemCtx *ctx, ASTNode *node, const char *fmt, ...);
void sem_hint(SemCtx *ctx, ASTNode *node, const char *msg);
void sem_reason(SemCtx *ctx, int line, int col, const char *fmt, ...);
void sem_suggestion(SemCtx *ctx, ASTNode *node, const char *suggestion);

// Type Logic & Conversion
int are_types_equal(VarType a, VarType b);
int get_conversion_cost(VarType from, VarType to);
const char* type_to_str(VarType t);

// Fuzzy matching / Suggestions
const char* find_closest_type_name(SemCtx *ctx, const char *name);
const char* find_closest_func_name(SemCtx *ctx, const char *name);
const char* find_closest_var_name(SemCtx *ctx, const char *name);

// Scope Management
void enter_scope(SemCtx *ctx);
void exit_scope(SemCtx *ctx);
void add_symbol_semantic(SemCtx *ctx, const char *name, VarType type, int is_mut, int is_arr, int arr_size, int line, int col);
SemSymbol* find_symbol_current_scope(SemCtx *ctx, const char *name);
SemSymbol* find_symbol_semantic(SemCtx *ctx, const char *name);

// Symbol Table Management (Functions, Classes, Enums)
void add_func(SemCtx *ctx, const char *name, char *mangled, VarType ret, VarType *params, int pcount, int is_flux);
SemFunc* resolve_overload(SemCtx *ctx, ASTNode *call_node, const char *name, ASTNode *args_list);
SemFunc* find_sem_func(SemCtx *ctx, const char *name);

SemEnum* find_sem_enum(SemCtx *ctx, const char *name);
void add_class(SemCtx *ctx, const char *name, const char *parent, char **traits, int trait_count, int is_union);
SemClass* find_sem_class(SemCtx *ctx, const char *name);
SemSymbol* find_member(SemCtx *ctx, const char *class_name, const char *member_name);
int class_has_trait(SemCtx *ctx, const char *class_name, const char *trait_name);
SemFunc* resolve_method_in_hierarchy(SemCtx *ctx, ASTNode *call_node, const char *class_name, const char *method_name, ASTNode *args, char **out_owner_class);

void add_typedef(SemCtx *ctx, const char *name, VarType target);
VarType resolve_typedef(SemCtx *ctx, VarType t);

// Driver Logic
void scan_declarations(SemCtx *ctx, ASTNode *node, const char *prefix);
void check_program(SemCtx *ctx, ASTNode *node);

int semantic_analysis(ASTNode *root, const char *source, const char *filename);

#endif // SEMANTIC_INTERNAL_H
