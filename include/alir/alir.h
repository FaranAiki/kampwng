#ifndef ALIR_H
#define ALIR_H

#include "../semantic/semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- ALIR TYPES ---

typedef enum {
    ALIR_VAL_VOID,
    ALIR_VAL_INT,
    ALIR_VAL_FLOAT,
    ALIR_VAL_STRING,
    ALIR_VAL_VAR,       // Represents a global/function name (@name)
    ALIR_VAL_TEMP,      // Represents a temporary register (%0, %1)
    ALIR_VAL_LABEL,     // Represents a block label
    ALIR_VAL_CONST,     // Raw constant value
    ALIR_VAL_TYPE,      // Represents a type (for sizeof)
    ALIR_VAL_GLOBAL     // Represents a global variable/constant pointer
} AlirValueKind;

typedef struct AlirValue {
    AlirValueKind kind;
    VarType type;       // Reuse Parser's VarType for type info
    
    // Value storage
    union {
        long int_val;
        double float_val;
        char *str_val;  // For names, string literals, or type names
        int temp_id;    // For temporaries
    };
} AlirValue;

typedef enum {
    // Memory & Access
    ALIR_OP_ALLOCA,
    ALIR_OP_FREE_STACK,
    ALIR_OP_STORE,
    ALIR_OP_LOAD,
    ALIR_OP_GET_PTR,    // Generic GEP (Get Element Ptr) for Arrays/Structs
    ALIR_OP_BITCAST,    // Replaces raw casting logic
    
    // New Memory Ops for Lowering
    ALIR_OP_ALLOC_HEAP, // malloc
    ALIR_OP_SIZEOF,     // sizeof(T)
    ALIR_OP_FREE_HEAP,       // free

    // Arithmetic
    ALIR_OP_ADD, ALIR_OP_SUB, ALIR_OP_MUL, ALIR_OP_DIV, ALIR_OP_MOD,
    ALIR_OP_FADD, ALIR_OP_FSUB, ALIR_OP_FMUL, ALIR_OP_FDIV,
    
    // Logical / Bitwise
    ALIR_OP_AND, ALIR_OP_OR, ALIR_OP_XOR, ALIR_OP_NOT, 
    ALIR_OP_SHL, ALIR_OP_SHR,
    
    // Comparison
    ALIR_OP_LT, ALIR_OP_GT, ALIR_OP_LTE, ALIR_OP_GTE, ALIR_OP_EQ, ALIR_OP_NEQ,
    
    // Control Flow
    ALIR_OP_JUMP,         // Unconditional Branch
    ALIR_OP_CONDI,    // Conditional Branch
    ALIR_OP_SWITCH,     // Switch (Complex Flow)
    ALIR_OP_CALL,
    ALIR_OP_RET,
    
    // Flux / Coroutines
    ALIR_OP_YIELD,      // High-level yield (lowers to state machine)
    
    // Iteration (High Level)
    ALIR_OP_ITER_INIT,  // Initialize iterator from collection
    ALIR_OP_ITER_VALID, // Check if iterator is valid
    ALIR_OP_ITER_NEXT,  // Advance iterator
    ALIR_OP_ITER_GET,   // Get current value from iterator

    // Misc
    ALIR_OP_CAST,
    ALIR_OP_PHI,        
    ALIR_OP_MOV
} AlirOpcode;

typedef struct AlirInst {
    AlirOpcode op;
    AlirValue *dest;        // Result (e.g., %1 = ...)
    AlirValue *op1;         // First operand
    AlirValue *op2;         // Second operand (optional)
    
    // For Calls or Switches
    AlirValue **args;       
    int arg_count;
    
    // For Switches (Cases map to labels)
    struct AlirSwitchCase *cases; 
    
    struct AlirInst *next;

    // Source mapping context
    int line;
    int col;
} AlirInst;

typedef struct AlirSwitchCase {
    long value;
    char *label;
    struct AlirSwitchCase *next;
} AlirSwitchCase;

typedef struct AlirBlock {
    int id;                 // Block ID (L1, L2...)
    char *label;            // Human readable label
    AlirInst *head;
    AlirInst *tail;
    struct AlirBlock *next;
} AlirBlock;

typedef struct AlirParam {
    char *name;
    VarType type;
    struct AlirParam *next;
} AlirParam;

typedef struct AlirFunction {
    char *name;
    VarType ret_type;
    
    // Params
    AlirParam *params;
    int param_count;

    AlirBlock *blocks;
    int block_count;
    int is_flux;
    int is_varargs;
    struct AlirFunction *next;
} AlirFunction;

// --- EXPLICIT STRUCT & ENUM DEFINITIONS ---

typedef struct AlirField {
    char *name;
    VarType type;
    int index;
    struct AlirField *next;
} AlirField;

typedef struct AlirStruct {
    char *name;
    AlirField *fields;
    int field_count;
    struct AlirStruct *next;
} AlirStruct;

typedef struct AlirEnumEntry {
    char *name;
    long value;
    struct AlirEnumEntry *next;
} AlirEnumEntry;

typedef struct AlirEnum {
    char *name;
    AlirEnumEntry *entries;
    struct AlirEnum *next;
} AlirEnum;

typedef struct AlirGlobal {
    char *name;
    char *string_content; // If string constant
    VarType type;
    struct AlirGlobal *next;
} AlirGlobal;

typedef struct AlirModule {
    char *name;
    AlirGlobal *globals;    // Global constants (strings)
    AlirFunction *functions;
    AlirStruct *structs;    // Registry of struct definitions
    AlirEnum *enums;        // Registry of enum definitions
    CompilerContext *compiler_ctx; // Reference for Arena
    
    // Diagnostics tracing
    const char *src;
    const char *filename;
} AlirModule;

// --- GENERATION CONTEXT ---

// Internal Map: Name -> ALIR Value (Pointer to Alloca)
typedef struct AlirSymbol {
    char *name;
    AlirValue *ptr; 
    VarType type;
    struct AlirSymbol *next;
} AlirSymbol;

// Flux Variable Tracking
typedef struct FluxVar {
    char *name;
    VarType type;
    int index; // Index in the context struct
    struct FluxVar *next;
} FluxVar;

typedef struct AlirCtx {
    SemanticCtx *sem;       // Reference to Semantic Context for Type Resolution
    
    AlirModule *module;
    AlirFunction *current_func;
    AlirBlock *current_block;
    
    AlirSymbol *symbols;    // Local IR Symbol Table (Name -> Register)
    
    int temp_counter;       
    int label_counter;
    int str_counter;        // For naming global strings
    
    // Loop Context
    AlirBlock *loop_continue;
    AlirBlock *loop_break;
    struct AlirCtx *loop_parent;

    // Flux Generation Context
    int in_flux_resume;
    FluxVar *flux_vars;
    AlirValue *flux_ctx_ptr;       // The %ctx pointer in Resume
    char *flux_struct_name;        // Name of the struct
    int flux_yield_count;
    AlirInst *flux_resume_switch;  // The switch instruction being built

    // AST Source Tracking 
    int current_line;
    int current_col;
} AlirCtx;

// Struct & Enum Registry
void alir_register_struct(AlirModule *mod, const char *name, AlirField *fields);
AlirStruct* alir_find_struct(AlirModule *mod, const char *name);
int alir_get_field_index(AlirModule *mod, const char *struct_name, const char *field_name);

void alir_register_enum(AlirModule *mod, const char *name, AlirEnumEntry *entries);
AlirEnum* alir_find_enum(AlirModule *mod, const char *name);
int alir_get_enum_value(AlirModule *mod, const char *enum_name, const char *entry_name, long *out_val);

// Generator Entry
// REQUIRES: Semantic Context (populated via sem_check_program)
AlirModule* alir_generate(SemanticCtx *sem, ASTNode *root);

void alir_print(AlirModule *mod);
void alir_emit_to_file(AlirModule *mod, const char *filename);

void emit(AlirCtx *ctx, AlirInst *i);

AlirInst* mk_inst(AlirModule *mod, AlirOpcode op, AlirValue *dest, AlirValue *op1, AlirValue *op2);
AlirValue* new_temp(AlirCtx *ctx, VarType t);
AlirValue* promote(AlirCtx *ctx, AlirValue *v, VarType target);
void alir_add_symbol(AlirCtx *ctx, const char *name, AlirValue *ptr, VarType t);
AlirSymbol* alir_find_symbol(AlirCtx *ctx, const char *name);

void alir_gen_flux_def(AlirCtx *ctx, FuncDefNode *fn);
void alir_gen_flux_yield(AlirCtx *ctx, EmitNode *en);
void collect_flux_vars_recursive(AlirCtx *ctx, ASTNode *node, int *idx_ptr);

// Constant Folding / Eval
long alir_eval_constant_int(AlirCtx *ctx, ASTNode *node);

AlirValue* alir_lower_new_object(AlirCtx *ctx, const char *class_name, ASTNode *args);

void alir_gen_switch(AlirCtx *ctx, SwitchNode *sn);
void alir_scan_and_register_classes(AlirCtx *ctx, ASTNode *root);

const char* alir_op_str(AlirOpcode op);

// Helpers to access arena
void* alir_alloc(AlirModule *mod, size_t size);
char* alir_strdup(AlirModule *mod, const char *str);

#include "lvalue.h"
#include "const.h"
#include "stmt.h"
#include "generator.h"
#include "core.h"
#include "flux.h"

#include "fragment/generate.h"
#include "fragment/addr.h"

#endif // ALIR_H
