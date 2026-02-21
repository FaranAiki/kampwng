#include "../../include/alick/alick_internal.h"

void alick_check_types(AlickCtx *ctx, AlirFunction *func) {
    AlirBlock *b = func->blocks;
    
    while (b) {
        AlirInst *i = b->head;
        while (i) {
            switch (i->op) {
                case ALIR_OP_STORE:
                    if (!i->op1) alick_error(ctx, func, b, i, "STORE requires a value operand (op1).");
                    if (!i->op2) alick_error(ctx, func, b, i, "STORE requires a target pointer operand (op2).");
                    break;
                    
                case ALIR_OP_LOAD:
                    if (!i->dest) alick_error(ctx, func, b, i, "LOAD requires a destination operand.");
                    if (!i->op1) alick_error(ctx, func, b, i, "LOAD requires a source pointer operand (op1).");
                    break;
                    
                case ALIR_OP_GET_PTR:
                    if (!i->dest) alick_error(ctx, func, b, i, "GET_PTR requires a destination operand.");
                    if (!i->op1) alick_error(ctx, func, b, i, "GET_PTR requires a base pointer operand (op1).");
                    if (!i->op2) alick_error(ctx, func, b, i, "GET_PTR requires an index operand (op2).");
                    break;
                    
                case ALIR_OP_ALLOC_HEAP:
                case ALIR_OP_ALLOCA:
                    if (!i->dest) alick_error(ctx, func, b, i, "Allocation instruction requires a destination pointer.");
                    break;
                    
                case ALIR_OP_FREE:
                    if (!i->op1) alick_error(ctx, func, b, i, "FREE requires a pointer operand (op1).");
                    break;

                case ALIR_OP_ADD:
                case ALIR_OP_SUB:
                case ALIR_OP_MUL:
                case ALIR_OP_DIV:
                case ALIR_OP_MOD:
                case ALIR_OP_FADD:
                case ALIR_OP_FSUB:
                case ALIR_OP_FMUL:
                case ALIR_OP_FDIV:
                case ALIR_OP_AND:
                case ALIR_OP_OR:
                case ALIR_OP_XOR:
                case ALIR_OP_SHL:
                case ALIR_OP_SHR:
                case ALIR_OP_LT:
                case ALIR_OP_GT:
                case ALIR_OP_LTE:
                case ALIR_OP_GTE:
                case ALIR_OP_EQ:
                case ALIR_OP_NEQ:
                    if (!i->dest) alick_error(ctx, func, b, i, "Binary op requires a destination.");
                    if (!i->op1 || !i->op2) alick_error(ctx, func, b, i, "Binary op requires two operands (op1, op2).");
                    break;
                    
                case ALIR_OP_NOT:
                case ALIR_OP_CAST:
                case ALIR_OP_BITCAST:
                    if (!i->dest) alick_error(ctx, func, b, i, "Unary/Cast op requires a destination.");
                    if (!i->op1) alick_error(ctx, func, b, i, "Unary/Cast op requires a source operand (op1).");
                    break;
                    
                case ALIR_OP_CALL:
                    if (!i->op1) alick_error(ctx, func, b, i, "CALL requires a function target (op1).");
                    // Note: dest is optional for CALL if return type is void
                    break;
                    
                default:
                    break;
            }
            
            i = i->next;
        }
        b = b->next;
    }
}
