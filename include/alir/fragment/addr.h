#ifndef ALIR_FRAGMENT_ADDR_H
#define ALIR_FRAGMENT_ADDR_H

AlirValue* alir_gen_addr_var_ref(AlirCtx *ctx, ASTNode *node);
AlirValue* alir_gen_addr_member_access(AlirCtx *ctx, ASTNode *node);

#endif // ALIR_FRAGMENT_ADDR_H
