#ifndef ALIR_FRAGMENT_GENERATE_H
#define ALIR_FRAGMENT_GENERATE_H

void alir_stmt_vardecl(AlirCtx *ctx, ASTNode *node);
void alir_stmt_assign(AlirCtx *ctx, ASTNode *node);
void alir_stmt_while(AlirCtx *ctx, ASTNode *node);
void alir_stmt_for_in(AlirCtx *ctx, ASTNode *node);

#endif // ALIR_FRAGMENT_GENERATE_H
