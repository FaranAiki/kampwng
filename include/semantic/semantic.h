#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "../parser/parser.h"
#include "../common/common.h"
#include "../common/diagnostic.h"
#include "../common/context.h"
#include <stdbool.h>

#include "typestruct.h"

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
