#ifndef ALIR_STMT_H
#define ALIR_STMT_H

#include "alir.h"

// Internal gen prototypes
AlirValue* alir_gen_expr(AlirCtx *ctx, ASTNode *node);
void alir_gen_stmt(AlirCtx *ctx, ASTNode *node);

#endif // ALIR_STMT_H
