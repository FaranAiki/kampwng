#ifndef ALIR_H
#define ALIR_H

#include "../parser/parser.h"
#include <stdio.h>

// --- ALIR TYPES ---

typedef enum {
    ALIR_VAL_VOID,
    ALIR_VAL_INT,
    ALIR_VAL_FLOAT,
    ALIR_VAL_STRING,
    ALIR_VAL_VAR,       // Represents a variable name / pointer
    ALIR_VAL_TEMP,      // Represents a temporary register (%0, %1)
    ALIR_VAL_LABEL,     // Represents a block label
    ALIR_VAL_CONST      // Raw constant value
} AlirValueKind;

typedef struct AlirValue {
    AlirValueKind kind;
    VarType type;       // Reuse Parser's VarType for type info
    
    // Value storage
    union {
        long int_val;
        double float_val;
        char *str_val;  // For names or string literals
        int temp_id;    // For temporaries
    };
} AlirValue;

typedef enum {
    // Memory & Access
    ALIR_OP_ALLOCA,
    ALIR_OP_STORE,
    ALIR_OP_LOAD,
    ALIR_OP_GET_PTR,    // Generic GEP (Get Element Ptr) for Arrays/Structs
    ALIR_OP_BITCAST,    // Replaces raw casting logic

    // Arithmetic
    ALIR_OP_ADD, ALIR_OP_SUB, ALIR_OP_MUL, ALIR_OP_DIV, ALIR_OP_MOD,
    ALIR_OP_FADD, ALIR_OP_FSUB, ALIR_OP_FMUL, ALIR_OP_FDIV,
    
    // Logical / Bitwise
    ALIR_OP_AND, ALIR_OP_OR, ALIR_OP_XOR, ALIR_OP_NOT, 
    ALIR_OP_SHL, ALIR_OP_SHR,
    
    // Comparison
    ALIR_OP_LT, ALIR_OP_GT, ALIR_OP_LTE, ALIR_OP_GTE, ALIR_OP_EQ, ALIR_OP_NEQ,
    
    // Control Flow
    ALIR_OP_BR,         // Unconditional Branch
    ALIR_OP_COND_BR,    // Conditional Branch
    ALIR_OP_SWITCH,     // Switch (Complex Flow)
    ALIR_OP_CALL,
    ALIR_OP_RET,
    
    // Flux / Coroutines
    ALIR_OP_YIELD,      // High-level yield (lowers to state machine)
    
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

typedef struct AlirFunction {
    char *name;
    VarType ret_type;
    AlirBlock *blocks;
    int block_count;
    int is_flux;
    struct AlirFunction *next;
} AlirFunction;

typedef struct AlirModule {
    char *name;
    AlirFunction *functions;
} AlirModule;

// --- SEMANTIC INFO FOR ALIR ---
// We need rudimentary class info to calculate struct indices for GEP

typedef struct AlirMember {
    char *name;
    int index;
    struct AlirMember *next;
} AlirMember;

typedef struct AlirClass {
    char *name;
    AlirMember *members;
    struct AlirClass *next;
} AlirClass;

// --- GENERATION CONTEXT ---

typedef struct AlirSymbol {
    char *name;
    AlirValue *ptr; 
    VarType type;
    struct AlirSymbol *next;
} AlirSymbol;

typedef struct AlirCtx {
    AlirModule *module;
    AlirFunction *current_func;
    AlirBlock *current_block;
    
    AlirSymbol *symbols;    // Symbol Table
    AlirClass *classes;     // Class Table (for member offsets)
    
    int temp_counter;       
    int label_counter;      
    
    // Loop Context
    AlirBlock *loop_continue;
    AlirBlock *loop_break;
    struct AlirCtx *loop_parent; 
} AlirCtx;

// --- API ---

// Core
AlirModule* alir_create_module(const char *name);
AlirFunction* alir_add_function(AlirModule *mod, const char *name, VarType ret, int is_flux);
AlirBlock* alir_add_block(AlirFunction *func, const char *label_hint);
void alir_append_inst(AlirBlock *block, AlirInst *inst);

// Value Creators
AlirValue* alir_const_int(long val);
AlirValue* alir_const_float(double val);
AlirValue* alir_val_temp(VarType t, int id);
AlirValue* alir_val_var(const char *name);
AlirValue* alir_val_label(const char *label);

// Generator Entry
AlirModule* alir_generate(ASTNode *root);
void alir_print(AlirModule *mod);
void alir_emit_to_file(AlirModule *mod, const char *filename);

#endif
