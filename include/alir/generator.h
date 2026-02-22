#ifndef ALIR_GENERATOR_H 
#define ALIR_GENERATOR_H

#include "alir.h"

void push_loop(AlirCtx *ctx, AlirBlock *cont, AlirBlock *brk);
void pop_loop(AlirCtx *ctx);
int is_terminator(AlirOpcode op); 

long alir_eval_constant_int(AlirCtx *ctx, ASTNode *node);
// TODO change this class node 
ClassNode* find_class_node(ASTNode *root, const char *name);
void build_struct_fields(AlirCtx *ctx, ASTNode *root, ClassNode *cn, AlirStruct *st);
void pass1_register(AlirCtx *ctx, ASTNode *n);
void pass2_populate(AlirCtx *ctx, ASTNode *root, ASTNode *n);
void alir_scan_and_register_classes(AlirCtx *ctx, ASTNode *root);
void alir_gen_switch(AlirCtx *ctx, SwitchNode *sn);
void alir_gen_implicit_constructor(AlirCtx *ctx, ClassNode *cn);
void alir_gen_function_def(AlirCtx *ctx, FuncDefNode *fn, const char *class_name);
void alir_gen_functions_recursive(AlirCtx *ctx, ASTNode *root);
AlirModule* alir_generate(SemanticCtx *sem, ASTNode *root);

#endif // ALIR_GENERATOR_H

