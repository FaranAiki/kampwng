#include "alir.h"
#include <stdlib.h>
#include <string.h>

// --- HELPERS ---

AlirInst* mk_inst(AlirOpcode op, AlirValue *dest, AlirValue *op1, AlirValue *op2) {
    AlirInst *i = calloc(1, sizeof(AlirInst));
    i->op = op;
    i->dest = dest;
    i->op1 = op1;
    i->op2 = op2;
    return i;
}

void emit(AlirCtx *ctx, AlirInst *i) {
    if (!ctx->current_block) return;
    alir_append_inst(ctx->current_block, i);
}

AlirValue* new_temp(AlirCtx *ctx, VarType t) {
    return alir_val_temp(t, ctx->temp_counter++);
}

AlirValue* promote(AlirCtx *ctx, AlirValue *v, VarType target) {
    if (v->type.base == target.base) return v;
    AlirValue *dest = new_temp(ctx, target);
    emit(ctx, mk_inst(ALIR_OP_CAST, dest, v, NULL));
    return dest;
}

// Symbol Table
void alir_add_symbol(AlirCtx *ctx, const char *name, AlirValue *ptr, VarType t) {
    AlirSymbol *s = calloc(1, sizeof(AlirSymbol));
    s->name = strdup(name);
    s->ptr = ptr;
    s->type = t;
    s->next = ctx->symbols;
    ctx->symbols = s;
}

AlirSymbol* alir_find_symbol(AlirCtx *ctx, const char *name) {
    AlirSymbol *s = ctx->symbols;
    while(s) {
        if (strcmp(s->name, name) == 0) return s;
        s = s->next;
    }
    return NULL;
}

// Loop Stack
void push_loop(AlirCtx *ctx, AlirBlock *cont, AlirBlock *brk) {
    AlirCtx *node = malloc(sizeof(AlirCtx));
    node->loop_continue = ctx->loop_continue;
    node->loop_break = ctx->loop_break;
    node->loop_parent = ctx->loop_parent;
    ctx->loop_parent = node;
    ctx->loop_continue = cont;
    ctx->loop_break = brk;
}

void pop_loop(AlirCtx *ctx) {
    if (!ctx->loop_parent) return;
    AlirCtx *node = ctx->loop_parent;
    ctx->loop_continue = node->loop_continue;
    ctx->loop_break = node->loop_break;
    ctx->loop_parent = node->loop_parent;
    free(node);
}

// Helper: Calculate type of expression (rudimentary)
VarType alir_calc_type(AlirCtx *ctx, ASTNode *node) {
    VarType vt = {TYPE_INT, 0};
    if (node->type == NODE_LITERAL) vt = ((LiteralNode*)node)->var_type;
    if (node->type == NODE_VAR_REF) {
        AlirSymbol *s = alir_find_symbol(ctx, ((VarRefNode*)node)->name);
        if (s) vt = s->type;
    }
    // Attempt rudimentary inference for member access
    if (node->type == NODE_MEMBER_ACCESS) {
         MemberAccessNode *ma = (MemberAccessNode*)node;
         VarType obj_t = alir_calc_type(ctx, ma->object);
         // Lookup field type if possible (simplified here)
         if (obj_t.class_name) {
             // In a full compiler, we'd look up the field type in AlirStruct
         }
    }
    // Trait access returns the Trait type (represented as class)
    if (node->type == NODE_TRAIT_ACCESS) {
        TraitAccessNode *ta = (TraitAccessNode*)node;
        vt = (VarType){TYPE_CLASS, 1, strdup(ta->trait_name)};
    }
    return vt;
}

// --- LOWERING HELPER: Register Class Layout with Flattening ---
void alir_scan_and_register_classes(AlirCtx *ctx, ASTNode *root) {
    ASTNode *curr = root;
    while(curr) {
        if (curr->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)curr;
            
            AlirField *head = NULL;
            AlirField **tail = &head;
            int idx = 0;

            // 1. FLATTENING: Copy Parent Fields First
            if (cn->parent_name) {
                AlirStruct *parent = alir_find_struct(ctx->module, cn->parent_name);
                if (parent) {
                    AlirField *pf = parent->fields;
                    while(pf) {
                        AlirField *nf = calloc(1, sizeof(AlirField));
                        nf->name = strdup(pf->name); // Copy name
                        nf->type = pf->type;
                        nf->index = idx++;
                        
                        *tail = nf;
                        tail = &nf->next;
                        pf = pf->next;
                    }
                }
            }

            // 2. Add Local Members
            ASTNode *mem = cn->members;
            while(mem) {
                if (mem->type == NODE_VAR_DECL) {
                    VarDeclNode *vd = (VarDeclNode*)mem;
                    AlirField *f = calloc(1, sizeof(AlirField));
                    f->name = strdup(vd->name);
                    f->type = vd->var_type;
                    f->index = idx++;
                    
                    *tail = f;
                    tail = &f->next;
                }
                mem = mem->next;
            }
            
            alir_register_struct(ctx->module, cn->name, head);
        } else if (curr->type == NODE_NAMESPACE) {
             alir_scan_and_register_classes(ctx, ((NamespaceNode*)curr)->body);
        }
        curr = curr->next;
    }
}

// --- LOWERING CONSTRUCTOR ---
AlirValue* alir_lower_new_object(AlirCtx *ctx, const char *class_name, ASTNode *args) {
    AlirStruct *st = alir_find_struct(ctx->module, class_name);
    if (!st) return NULL; 

    // 1. Sizeof
    AlirValue *size_val = new_temp(ctx, (VarType){TYPE_INT, 0});
    AlirInst *i_size = mk_inst(ALIR_OP_SIZEOF, size_val, alir_val_type(class_name), NULL);
    emit(ctx, i_size);

    // 2. Alloc Heap (Malloc)
    AlirValue *raw_mem = new_temp(ctx, (VarType){TYPE_CHAR, 1}); // char*
    emit(ctx, mk_inst(ALIR_OP_ALLOC_HEAP, raw_mem, size_val, NULL));

    // 3. Bitcast to Class*
    VarType cls_ptr_type = {TYPE_CLASS, 1, strdup(class_name)};
    AlirValue *obj_ptr = new_temp(ctx, cls_ptr_type);
    emit(ctx, mk_inst(ALIR_OP_BITCAST, obj_ptr, raw_mem, NULL));

    // 4. Call Constructor
    AlirInst *call_init = mk_inst(ALIR_OP_CALL, NULL, alir_val_var(class_name), NULL);
    
    int arg_count = 0; ASTNode *a = args; while(a) { arg_count++; a=a->next; }
    call_init->arg_count = arg_count + 1;
    call_init->args = malloc(sizeof(AlirValue*) * (arg_count + 1));
    
    call_init->args[0] = obj_ptr; // THIS pointer
    
    int i = 1; a = args;
    while(a) {
        call_init->args[i++] = alir_gen_expr(ctx, a);
        a = a->next;
    }
    
    call_init->dest = new_temp(ctx, (VarType){TYPE_VOID, 0});
    emit(ctx, call_init);
    
    return obj_ptr;
}


// --- L-VALUE GENERATION ---

AlirValue* alir_gen_addr(AlirCtx *ctx, ASTNode *node) {
    if (node->type == NODE_VAR_REF) {
        VarRefNode *vn = (VarRefNode*)node;
        AlirSymbol *sym = alir_find_symbol(ctx, vn->name);
        if (sym) return sym->ptr;
        return alir_val_var(vn->name);
    }
    
    if (node->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)node;
        AlirValue *base_ptr = alir_gen_addr(ctx, ma->object);
        if (!base_ptr) base_ptr = alir_gen_expr(ctx, ma->object);

        VarType obj_t = alir_calc_type(ctx, ma->object);
        
        if (obj_t.class_name) {
            int idx = alir_get_field_index(ctx->module, obj_t.class_name, ma->member_name);
            if (idx != -1) {
                AlirValue *res = new_temp(ctx, (VarType){TYPE_INT, 1}); 
                emit(ctx, mk_inst(ALIR_OP_GET_PTR, res, base_ptr, alir_const_int(idx)));
                return res;
            }
        }
    }
    
    if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *aa = (ArrayAccessNode*)node;
        AlirValue *base_ptr = alir_gen_addr(ctx, aa->target);
        AlirValue *index = alir_gen_expr(ctx, aa->index);
        
        AlirValue *res = new_temp(ctx, (VarType){TYPE_INT, 1});
        emit(ctx, mk_inst(ALIR_OP_GET_PTR, res, base_ptr, index));
        return res;
    }
    
    return NULL;
}

// --- TRAIT ACCESS GEN ---
// Calculates the pointer to the Trait part of an object.
// Uses Struct Flattening assumption or looks for a field with Trait name.
AlirValue* alir_gen_trait_access(AlirCtx *ctx, TraitAccessNode *ta) {
    AlirValue *base_ptr = alir_gen_addr(ctx, ta->object);
    if (!base_ptr) base_ptr = alir_gen_expr(ctx, ta->object);
    
    VarType obj_t = alir_calc_type(ctx, ta->object);
    
    // 1. Try to find a field named after the Trait (Mixin strategy)
    if (obj_t.class_name) {
        int idx = alir_get_field_index(ctx->module, obj_t.class_name, ta->trait_name);
        if (idx != -1) {
            // Found explicit field for trait
            AlirValue *res = new_temp(ctx, (VarType){TYPE_CLASS, 1, strdup(ta->trait_name)});
            emit(ctx, mk_inst(ALIR_OP_GET_PTR, res, base_ptr, alir_const_int(idx)));
            return res;
        }
    }
    
    // 2. Fallback: Bitcast (Unsafe/Direct Cast)
    // If the layout implies the object IS the trait (first position or phantom), cast it.
    VarType trait_ptr_t = {TYPE_CLASS, 1, strdup(ta->trait_name)};
    AlirValue *cast_res = new_temp(ctx, trait_ptr_t);
    emit(ctx, mk_inst(ALIR_OP_BITCAST, cast_res, base_ptr, NULL));
    return cast_res;
}

AlirValue* alir_gen_literal(AlirCtx *ctx, LiteralNode *ln) {
    if (ln->var_type.base == TYPE_INT) return alir_const_int(ln->val.int_val);
    if (ln->var_type.base == TYPE_FLOAT) return alir_const_float(ln->val.double_val);
    if (ln->var_type.base == TYPE_STRING) {
        // Updated: Use Global String Pool
        return alir_module_add_string_literal(ctx->module, ln->val.str_val, ctx->str_counter++);
    }
    return alir_const_int(0);
}

AlirValue* alir_gen_var_ref(AlirCtx *ctx, VarRefNode *vn) {
    AlirValue *ptr = alir_gen_addr(ctx, (ASTNode*)vn);
    AlirSymbol *sym = alir_find_symbol(ctx, vn->name);
    VarType t = sym ? sym->type : (VarType){TYPE_INT, 0};
    
    AlirValue *val = new_temp(ctx, t);
    emit(ctx, mk_inst(ALIR_OP_LOAD, val, ptr, NULL));
    return val;
}

AlirValue* alir_gen_access(AlirCtx *ctx, ASTNode *node) {
    AlirValue *ptr = alir_gen_addr(ctx, node);
    AlirValue *val = new_temp(ctx, (VarType){TYPE_INT, 0}); 
    emit(ctx, mk_inst(ALIR_OP_LOAD, val, ptr, NULL));
    return val;
}

AlirValue* alir_gen_binary_op(AlirCtx *ctx, BinaryOpNode *bn) {
    AlirValue *l = alir_gen_expr(ctx, bn->left);
    AlirValue *r = alir_gen_expr(ctx, bn->right);
    
    if (l->type.base == TYPE_FLOAT || r->type.base == TYPE_FLOAT) {
        l = promote(ctx, l, (VarType){TYPE_FLOAT, 0});
        r = promote(ctx, r, (VarType){TYPE_FLOAT, 0});
    }

    AlirOpcode op = ALIR_OP_ADD;
    switch(bn->op) {
        case TOKEN_PLUS: op = ALIR_OP_ADD; break;
        case TOKEN_MINUS: op = ALIR_OP_SUB; break;
        case TOKEN_STAR: op = ALIR_OP_MUL; break;
        case TOKEN_SLASH: op = ALIR_OP_DIV; break;
        case TOKEN_EQ: op = ALIR_OP_EQ; break;
        case TOKEN_LT: op = ALIR_OP_LT; break;
    }
    
    AlirValue *dest = new_temp(ctx, l->type);
    emit(ctx, mk_inst(op, dest, l, r));
    return dest;
}

AlirValue* alir_gen_call_std(AlirCtx *ctx, CallNode *cn) {
    AlirInst *call = mk_inst(ALIR_OP_CALL, NULL, alir_val_var(cn->name), NULL);
    
    int count = 0; ASTNode *a = cn->args; while(a) { count++; a=a->next; }
    call->arg_count = count;
    call->args = malloc(sizeof(AlirValue*) * count);
    
    int i = 0; a = cn->args;
    while(a) {
        call->args[i++] = alir_gen_expr(ctx, a);
        a = a->next;
    }
    
    AlirValue *dest = new_temp(ctx, (VarType){TYPE_INT, 0}); 
    call->dest = dest;
    emit(ctx, call);
    return dest;
}

AlirValue* alir_gen_call(AlirCtx *ctx, CallNode *cn) {
    if (alir_find_struct(ctx->module, cn->name)) {
        return alir_lower_new_object(ctx, cn->name, cn->args);
    }
    return alir_gen_call_std(ctx, cn);
}

AlirValue* alir_gen_method_call(AlirCtx *ctx, MethodCallNode *mc) {
    AlirValue *this_ptr = alir_gen_addr(ctx, mc->object);
    if (!this_ptr) this_ptr = alir_gen_expr(ctx, mc->object); 

    // Mangle: Class_Method
    VarType obj_t = alir_calc_type(ctx, mc->object);
    char func_name[256];
    if (obj_t.class_name) snprintf(func_name, 256, "%s_%s", obj_t.class_name, mc->method_name);
    else snprintf(func_name, 256, "%s", mc->method_name);

    AlirInst *call = mk_inst(ALIR_OP_CALL, NULL, alir_val_var(func_name), NULL);
    
    int count = 0; ASTNode *a = mc->args; while(a) { count++; a=a->next; }
    call->arg_count = count + 1;
    call->args = malloc(sizeof(AlirValue*) * (count + 1));
    
    call->args[0] = this_ptr;
    int i = 1; a = mc->args;
    while(a) {
        call->args[i++] = alir_gen_expr(ctx, a);
        a = a->next;
    }
    
    AlirValue *dest = new_temp(ctx, (VarType){TYPE_INT, 0});
    call->dest = dest;
    emit(ctx, call);
    return dest;
}

AlirValue* alir_gen_expr(AlirCtx *ctx, ASTNode *node) {
    if (!node) return NULL;
    switch(node->type) {
        case NODE_LITERAL: return alir_gen_literal(ctx, (LiteralNode*)node);
        case NODE_VAR_REF: return alir_gen_var_ref(ctx, (VarRefNode*)node);
        case NODE_BINARY_OP: return alir_gen_binary_op(ctx, (BinaryOpNode*)node);
        case NODE_MEMBER_ACCESS: return alir_gen_access(ctx, node);
        case NODE_ARRAY_ACCESS: return alir_gen_access(ctx, node);
        case NODE_CALL: return alir_gen_call(ctx, (CallNode*)node);
        case NODE_METHOD_CALL: return alir_gen_method_call(ctx, (MethodCallNode*)node);
        case NODE_TRAIT_ACCESS: return alir_gen_trait_access(ctx, (TraitAccessNode*)node);
        default: return NULL;
    }
}

void alir_gen_switch(AlirCtx *ctx, SwitchNode *sn) {
    AlirValue *cond = alir_gen_expr(ctx, sn->condition);
    AlirBlock *end_bb = alir_add_block(ctx->current_func, "switch_end");
    AlirBlock *default_bb = end_bb; 
    
    if (sn->default_case) default_bb = alir_add_block(ctx->current_func, "switch_default");

    AlirInst *sw = mk_inst(ALIR_OP_SWITCH, NULL, cond, alir_val_label(default_bb->label));
    sw->cases = NULL;
    AlirSwitchCase **tail = &sw->cases;

    ASTNode *c = sn->cases;
    while(c) {
        CaseNode *cn = (CaseNode*)c;
        AlirBlock *case_bb = alir_add_block(ctx->current_func, "case");
        
        AlirSwitchCase *sc = calloc(1, sizeof(AlirSwitchCase));
        sc->label = case_bb->label;
        if (cn->value->type == NODE_LITERAL) 
            sc->value = ((LiteralNode*)cn->value)->val.int_val;
        
        *tail = sc;
        tail = &sc->next;
        
        c = c->next;
    }
    emit(ctx, sw); 

    c = sn->cases;
    AlirSwitchCase *sc_iter = sw->cases;
    while(c) {
        CaseNode *cn = (CaseNode*)c;
        AlirBlock *case_bb = NULL;
        AlirBlock *search = ctx->current_func->blocks;
        while(search) { 
            if (strcmp(search->label, sc_iter->label) == 0) { case_bb = search; break; }
            search = search->next;
        }
        
        ctx->current_block = case_bb;
        push_loop(ctx, NULL, end_bb);
        
        ASTNode *stmt = cn->body;
        while(stmt) { alir_gen_stmt(ctx, stmt); stmt = stmt->next; }
        
        if (!cn->is_leak) emit(ctx, mk_inst(ALIR_OP_BR, NULL, alir_val_label(end_bb->label), NULL));
        
        pop_loop(ctx);
        c = c->next;
        sc_iter = sc_iter->next;
    }
    
    if (sn->default_case) {
        ctx->current_block = default_bb;
        push_loop(ctx, NULL, end_bb);
        ASTNode *stmt = sn->default_case;
        while(stmt) { alir_gen_stmt(ctx, stmt); stmt = stmt->next; }
        pop_loop(ctx);
        emit(ctx, mk_inst(ALIR_OP_BR, NULL, alir_val_label(end_bb->label), NULL));
    }
    
    ctx->current_block = end_bb;
}

void alir_gen_flux_yield(AlirCtx *ctx, EmitNode *en) {
    AlirValue *val = alir_gen_expr(ctx, en->value);
    emit(ctx, mk_inst(ALIR_OP_YIELD, NULL, val, NULL));
}

void alir_gen_stmt(AlirCtx *ctx, ASTNode *node) {
    if (!node) return;
    switch(node->type) {
        case NODE_VAR_DECL: {
            VarDeclNode *vn = (VarDeclNode*)node;
            AlirValue *ptr = new_temp(ctx, vn->var_type);
            emit(ctx, mk_inst(ALIR_OP_ALLOCA, ptr, NULL, NULL));
            alir_add_symbol(ctx, vn->name, ptr, vn->var_type);
            if (vn->initializer) {
                AlirValue *val = alir_gen_expr(ctx, vn->initializer);
                emit(ctx, mk_inst(ALIR_OP_STORE, NULL, val, ptr));
            }
            break;
        }
        case NODE_ASSIGN: {
            AssignNode *an = (AssignNode*)node;
            AlirValue *ptr = NULL;
            if (an->name) {
                AlirSymbol *s = alir_find_symbol(ctx, an->name);
                if (s) ptr = s->ptr;
            } else if (an->target) {
                ptr = alir_gen_addr(ctx, an->target);
            }
            AlirValue *val = alir_gen_expr(ctx, an->value);
            emit(ctx, mk_inst(ALIR_OP_STORE, NULL, val, ptr));
            break;
        }
        case NODE_SWITCH: alir_gen_switch(ctx, (SwitchNode*)node); break;
        case NODE_EMIT: alir_gen_flux_yield(ctx, (EmitNode*)node); break;
        case NODE_RETURN: {
            ReturnNode *rn = (ReturnNode*)node;
            AlirValue *v = rn->value ? alir_gen_expr(ctx, rn->value) : NULL;
            emit(ctx, mk_inst(ALIR_OP_RET, NULL, v, NULL));
            break;
        }
        case NODE_CALL: alir_gen_expr(ctx, node); break;
        case NODE_IF: {
            IfNode *in = (IfNode*)node;
            AlirValue *cond = alir_gen_expr(ctx, in->condition);
            AlirBlock *then_bb = alir_add_block(ctx->current_func, "then");
            AlirBlock *else_bb = alir_add_block(ctx->current_func, "else");
            AlirBlock *merge_bb = alir_add_block(ctx->current_func, "merge");
            
            AlirBlock *target_else = in->else_body ? else_bb : merge_bb;
            
            AlirInst *br = mk_inst(ALIR_OP_COND_BR, NULL, cond, alir_val_label(then_bb->label));
            br->args = malloc(sizeof(AlirValue*));
            br->args[0] = alir_val_label(target_else->label);
            br->arg_count = 1;
            emit(ctx, br);
            
            ctx->current_block = then_bb;
            ASTNode *s = in->then_body; while(s){ alir_gen_stmt(ctx,s); s=s->next; }
            emit(ctx, mk_inst(ALIR_OP_BR, NULL, alir_val_label(merge_bb->label), NULL));
            
            if (in->else_body) {
                ctx->current_block = else_bb;
                s = in->else_body; while(s){ alir_gen_stmt(ctx,s); s=s->next; }
                emit(ctx, mk_inst(ALIR_OP_BR, NULL, alir_val_label(merge_bb->label), NULL));
            }
            
            ctx->current_block = merge_bb;
            break;
        }
    }
}

AlirModule* alir_generate(ASTNode *root) {
    AlirCtx ctx;
    memset(&ctx, 0, sizeof(AlirCtx));
    ctx.module = alir_create_module("main_module");
    
    // 1. SCAN AND REGISTER CLASSES (Flattening included)
    alir_scan_and_register_classes(&ctx, root);
    
    // 2. GEN FUNCTIONS
    ASTNode *curr = root;
    while(curr) {
        if (curr->type == NODE_FUNC_DEF) {
            FuncDefNode *fn = (FuncDefNode*)curr;
            ctx.current_func = alir_add_function(ctx.module, fn->name, fn->ret_type, fn->is_flux);
            
            // Register parameters
            Parameter *p = fn->params;
            while(p) {
                alir_func_add_param(ctx.current_func, p->name, p->type);
                p = p->next;
            }

            // If function is external (no body), skip generation
            if (!fn->body) {
                curr = curr->next;
                continue;
            }

            ctx.current_block = alir_add_block(ctx.current_func, "entry");
            ctx.temp_counter = 0;
            ctx.symbols = NULL; 
            
            // Setup Params allocation
            p = fn->params;
            while(p) {
                AlirValue *ptr = new_temp(&ctx, p->type);
                emit(&ctx, mk_inst(ALIR_OP_ALLOCA, ptr, NULL, NULL));
                alir_add_symbol(&ctx, p->name, ptr, p->type);
                p = p->next;
            }
            
            ASTNode *stmt = fn->body;
            while(stmt) { alir_gen_stmt(&ctx, stmt); stmt = stmt->next; }
        } else if (curr->type == NODE_NAMESPACE) {
             // Recursive scan for functions inside namespaces would go here
        }
        curr = curr->next;
    }
    return ctx.module;
}
