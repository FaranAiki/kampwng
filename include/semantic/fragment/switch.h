#ifndef SEMANTIC_FRAGMENT_SWITCH_H
#define SEMANTIC_FRAGMENT_SWITCH_H 

#include "../semantic.h"

void sem_check_for_in(SemanticCtx *ctx, ASTNode *node);
void sem_check_unary_op_switch(SemanticCtx *ctx, ASTNode *node);
void sem_check_var_ref(SemanticCtx *ctx, ASTNode *node);
void sem_check_array_access(SemanticCtx *ctx, ASTNode *node);

#endif // SEMANTIC_FRAGMENT_SWITCH_H
