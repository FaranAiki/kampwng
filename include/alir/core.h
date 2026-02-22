#ifndef ALIR_CORE_H
#define ALIR_CORE_H

// Core
AlirModule* alir_create_module(CompilerContext *ctx, const char *name);
AlirFunction* alir_add_function(AlirModule *mod, const char *name, VarType ret, int is_flux);
void alir_func_add_param(AlirModule *mod, AlirFunction *func, const char *name, VarType type);
AlirValue* alir_module_add_string_literal(AlirModule *mod, const char *content, VarType type, int id_hint);

AlirBlock* alir_add_block(AlirModule *mod, AlirFunction *func, const char *label_hint);
void alir_append_inst(AlirBlock *block, AlirInst *inst);

#endif // ALIR_CORE_H
