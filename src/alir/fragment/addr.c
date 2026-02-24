#include "alir.h"

AlirValue* alir_gen_addr_var_ref(AlirCtx *ctx, ASTNode *node) {
    VarRefNode *vn = (VarRefNode*)node;
    if (vn->is_class_member) {
        AlirSymbol *this_sym = alir_find_symbol(ctx, "this");
        if (!this_sym) return NULL; 
        
        char *class_name = this_sym->type.class_name;
        int idx = alir_robust_get_field_index(ctx, class_name, vn->name);
        
        AlirValue *this_ptr = new_temp(ctx, this_sym->type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, this_ptr, this_sym->ptr, NULL));
        
        VarType mem_type = sem_get_node_type(ctx->sem, node);
        mem_type.ptr_depth++;
        AlirValue *res = new_temp(ctx, mem_type); 
        emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, res, this_ptr, alir_const_int(ctx->module, idx)));
        return res;
    }
    AlirSymbol *sym = alir_find_symbol(ctx, vn->name);
    if (sym) return sym->ptr;

    // [FIX] Implicit `this` indexing fallback for bare reads (e.g. `oxygen_level` vs `this.oxygen_level`)
    AlirSymbol *this_sym = alir_find_symbol(ctx, "this");
    if (this_sym && this_sym->type.class_name) {
        int idx = alir_get_field_index(ctx->module, this_sym->type.class_name, vn->name);
        if (idx != -1) {
            AlirValue *this_ptr = new_temp(ctx, this_sym->type);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, this_ptr, this_sym->ptr, NULL));
            
            VarType mem_type = sem_get_node_type(ctx->sem, node);
            if (mem_type.base == TYPE_UNKNOWN) mem_type = (VarType){TYPE_AUTO, 0, 0, NULL};
            mem_type.ptr_depth++;
            
            AlirValue *res = new_temp(ctx, mem_type); 
            emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, res, this_ptr, alir_const_int(ctx->module, idx)));
            return res;
        }
    }
    return NULL;
}

AlirValue* alir_gen_addr_member_access(AlirCtx *ctx, ASTNode *node) {
    MemberAccessNode *ma = (MemberAccessNode*)node;
    VarType obj_t = sem_get_node_type(ctx->sem, ma->object);
    if (obj_t.base == TYPE_ENUM) return NULL; 

    AlirValue *base_ptr = alir_gen_expr(ctx, ma->object);
    if (!base_ptr) return NULL;

    char *class_name = base_ptr->type.class_name;
    if (!class_name && obj_t.class_name) class_name = obj_t.class_name;
    if (!class_name && ma->object->type == NODE_VAR_REF) {
        AlirSymbol *sym = alir_find_symbol(ctx, ((VarRefNode*)ma->object)->name);
        if (sym && sym->type.class_name) class_name = sym->type.class_name;
    }

    int idx = alir_robust_get_field_index(ctx, class_name, ma->member_name);
    AlirValue *res = new_temp(ctx, (VarType){TYPE_AUTO, 1, 0, NULL}); 
    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, res, base_ptr, alir_const_int(ctx->module, idx)));
    return res;
}
