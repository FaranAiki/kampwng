#include "../../include/alick/alick_internal.h"
#include <string.h>

static int is_terminator(AlirOpcode op) {
    return op == ALIR_OP_RET || 
           op == ALIR_OP_JUMP || 
           op == ALIR_OP_CONDI || 
           op == ALIR_OP_SWITCH || 
           op == ALIR_OP_YIELD;
}

static AlirBlock* find_block(AlirFunction *func, const char *label) {
    AlirBlock *b = func->blocks;
    while (b) {
        if (strcmp(b->label, label) == 0) return b;
        b = b->next;
    }
    return NULL;
}

void alick_check_cfg(AlickCtx *ctx, AlirFunction *func) {
    AlirBlock *b = func->blocks;
    
    while (b) {
        // 1. Check if block is completely empty
        if (!b->head || !b->tail) {
            alick_error(ctx, func, b, NULL, "Block is empty. Must contain at least a terminator instruction.");
            b = b->next;
            continue;
        }

        // 2. Check if the final instruction is a valid terminator
        if (!is_terminator(b->tail->op)) {
            alick_error(ctx, func, b, b->tail, "Block lacks a terminator. Last instruction is '%s', expected branch/return.", alir_op_str(b->tail->op));
        }

        // 3. Verify Branch Targets Exist
        AlirInst *term = b->tail;
        if (term->op == ALIR_OP_JUMP) {
            if (!term->op1 || term->op1->kind != ALIR_VAL_LABEL) {
                alick_error(ctx, func, b, term, "Unconditional JUMP target must be a label.");
            } else if (!find_block(func, term->op1->str_val)) {
                alick_error(ctx, func, b, term, "JUMP target label '%s' does not exist in function.", term->op1->str_val);
            }
        } 
        else if (term->op == ALIR_OP_CONDI) {
            if (!term->op2 || term->op2->kind != ALIR_VAL_LABEL) {
                alick_error(ctx, func, b, term, "CONDI true-branch target must be a label.");
            } else if (!find_block(func, term->op2->str_val)) {
                alick_error(ctx, func, b, term, "CONDI true-branch label '%s' does not exist.", term->op2->str_val);
            }
            
            if (term->arg_count < 1 || !term->args || !term->args[0] || term->args[0]->kind != ALIR_VAL_LABEL) {
                alick_error(ctx, func, b, term, "CONDI false-branch target must be a label passed in args[0].");
            } else if (!find_block(func, term->args[0]->str_val)) {
                alick_error(ctx, func, b, term, "CONDI false-branch label '%s' does not exist.", term->args[0]->str_val);
            }
        } 
        else if (term->op == ALIR_OP_SWITCH) {
            if (!term->op2 || term->op2->kind != ALIR_VAL_LABEL) {
                alick_error(ctx, func, b, term, "SWITCH default-branch target must be a label in op2.");
            } else if (!find_block(func, term->op2->str_val)) {
                alick_error(ctx, func, b, term, "SWITCH default-branch label '%s' does not exist.", term->op2->str_val);
            }
            
            AlirSwitchCase *c = term->cases;
            while (c) {
                if (!find_block(func, c->label)) {
                    alick_error(ctx, func, b, term, "SWITCH case target label '%s' does not exist.", c->label);
                }
                c = c->next;
            }
        }

        // 4. Ensure no unreachable instructions exist AFTER the terminator
        AlirInst *i = b->head;
        while (i) {
            if (is_terminator(i->op) && i != b->tail) {
                alick_error(ctx, func, b, i, "Early terminator '%s' found. Instructions following this in the same block are unreachable.", alir_op_str(i->op));
            }
            i = i->next;
        }

        b = b->next;
    }
}
