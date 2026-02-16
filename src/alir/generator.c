#include "alir.h"
#include <stdlib.h>
#include <string.h>

// --- SCANNING & SEMANTICS ---
// We need to know class member indices to generate GEP instructions (struct offsets)

void alir_register_class(AlirCtx *ctx, ClassNode *cn) {
    AlirClass *cls = calloc(1, sizeof(AlirClass));
    cls->name = strdup(cn->name);
    
    AlirMember **tail = &cls->members;
    int idx = 0;
    
    // Simplification: In a real compiler, we would recursively resolve parent members
    // For ALIR demo, we scan local members
    ASTNode *m = cn->members;
    while(m) {
        if (m->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)m;
            AlirMember *am = calloc(1, sizeof(AlirMember));
            am->name = strdup(vd->name);
            am->index = idx++;
            *tail = am;
            tail = &am->next;
        }
        m = m->next;
    }
    
    cls->next = ctx->classes;
    ctx->classes = cls;
}

int alir_get_member_index(AlirCtx *ctx, const char *cls_name, const char *mem_name) {
    AlirClass *c = ctx->classes;
    while(c) {
        if (strcmp(c->name, cls_name) == 0) {
            AlirMember *m = c->members;
            while(m) {
                if (strcmp(m->name, mem_name) == 0) return m->index;
                m = m->next;
            }
        }
        c = c->next;
    }
    return -1;
}

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
    // This duplicates some logic from parser/codegen but is needed for temp allocation
    VarType vt = {TYPE_INT, 0};
    if (node->type == NODE_LITERAL) vt = ((LiteralNode*)node)->var_type;
    if (node->type == NODE_VAR_REF) {
        AlirSymbol *s = alir_find_symbol(ctx, ((VarRefNode*)node)->name);
        if (s) vt = s->type;
    }
    return vt;
}

// Forward Decls
AlirValue* alir_gen_expr(AlirCtx *ctx, ASTNode *node);
void alir_gen_stmt(AlirCtx *ctx, ASTNode *node);

// --- L-VALUE GENERATION (Address Calculation) ---

AlirValue* alir_gen_addr(AlirCtx *ctx, ASTNode *node) {
    if (node->type == NODE_VAR_REF) {
        VarRefNode *vn = (VarRefNode*)node;
        AlirSymbol *sym = alir_find_symbol(ctx, vn->name);
        if (sym) return sym->ptr;
        return alir_val_var(vn->name); // Global fallback
    }
    
    if (node->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)node;
        AlirValue *base_ptr = alir_gen_addr(ctx, ma->object);
        VarType obj_t = alir_calc_type(ctx, ma->object);
        
        if (obj_t.class_name) {
            int idx = alir_get_member_index(ctx, obj_t.class_name, ma->member_name);
            if (idx != -1) {
                // GEP: base + 0 (deref if pointer) + idx
                // In ALIR, we use GET_PTR op1=base, op2=index
                AlirValue *res = new_temp(ctx, (VarType){TYPE_INT, 1}); // Result is int* (abstract)
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

// --- EXPRESSION GENERATION ---

static AlirValue* gen_literal(AlirCtx *ctx, LiteralNode *ln) {
    if (ln->var_type.base == TYPE_INT) return alir_const_int(ln->val.int_val);
    if (ln->var_type.base == TYPE_FLOAT) return alir_const_float(ln->val.double_val);
    if (ln->var_type.base == TYPE_STRING) {
        AlirValue *v = calloc(1, sizeof(AlirValue));
        v->kind = ALIR_VAL_CONST;
        v->type = ln->var_type;
        v->str_val = strdup(ln->val.str_val);
        return v;
    }
    return alir_const_int(0);
}

static AlirValue* gen_var_ref(AlirCtx *ctx, VarRefNode *vn) {
    AlirValue *ptr = alir_gen_addr(ctx, (ASTNode*)vn);
    AlirSymbol *sym = alir_find_symbol(ctx, vn->name);
    VarType t = sym ? sym->type : (VarType){TYPE_INT, 0};
    
    AlirValue *val = new_temp(ctx, t);
    emit(ctx, mk_inst(ALIR_OP_LOAD, val, ptr, NULL));
    return val;
}

static AlirValue* gen_access(AlirCtx *ctx, ASTNode *node) {
    AlirValue *ptr = alir_gen_addr(ctx, node);
    AlirValue *val = new_temp(ctx, (VarType){TYPE_INT, 0}); // Simplified type inference
    emit(ctx, mk_inst(ALIR_OP_LOAD, val, ptr, NULL));
    return val;
}

static AlirValue* gen_binary_op(AlirCtx *ctx, BinaryOpNode *bn) {
    AlirValue *l = alir_gen_expr(ctx, bn->left);
    AlirValue *r = alir_gen_expr(ctx, bn->right);
    
    // Auto-promote
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
        // ... (Others mapped similarly)
    }
    
    AlirValue *dest = new_temp(ctx, l->type);
    emit(ctx, mk_inst(op, dest, l, r));
    return dest;
}

static AlirValue* gen_call(AlirCtx *ctx, CallNode *cn) {
    AlirInst *call = mk_inst(ALIR_OP_CALL, NULL, alir_val_var(cn->name), NULL);
    
    // Count args
    int count = 0; ASTNode *a = cn->args; while(a) { count++; a=a->next; }
    call->arg_count = count;
    call->args = malloc(sizeof(AlirValue*) * count);
    
    int i = 0; a = cn->args;
    while(a) {
        call->args[i++] = alir_gen_expr(ctx, a);
        a = a->next;
    }
    
    AlirValue *dest = new_temp(ctx, (VarType){TYPE_INT, 0}); // Assume int ret
    call->dest = dest;
    emit(ctx, call);
    return dest;
}

static AlirValue* gen_method_call(AlirCtx *ctx, MethodCallNode *mc) {
    // 1. Calculate 'this' pointer
    AlirValue *this_ptr = alir_gen_addr(ctx, mc->object);
    if (!this_ptr) this_ptr = alir_gen_expr(ctx, mc->object); // It might be an r-value pointer

    // 2. Resolve Name (Mangle: Class_Method)
    VarType obj_t = alir_calc_type(ctx, mc->object);
    char func_name[256];
    if (obj_t.class_name) snprintf(func_name, 256, "%s_%s", obj_t.class_name, mc->method_name);
    else snprintf(func_name, 256, "%s", mc->method_name);

    AlirInst *call = mk_inst(ALIR_OP_CALL, NULL, alir_val_var(func_name), NULL);
    
    // 3. Prepare Args (prepend 'this')
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
        case NODE_LITERAL: return gen_literal(ctx, (LiteralNode*)node);
        case NODE_VAR_REF: return gen_var_ref(ctx, (VarRefNode*)node);
        case NODE_BINARY_OP: return gen_binary_op(ctx, (BinaryOpNode*)node);
        case NODE_MEMBER_ACCESS: return gen_access(ctx, node);
        case NODE_ARRAY_ACCESS: return gen_access(ctx, node);
        case NODE_CALL: return gen_call(ctx, (CallNode*)node);
        case NODE_METHOD_CALL: return gen_method_call(ctx, (MethodCallNode*)node);
        // ... (Unary, Cast, etc)
        default: return NULL;
    }
}

// --- STATEMENT GENERATION ---

static void gen_switch(AlirCtx *ctx, SwitchNode *sn) {
    AlirValue *cond = alir_gen_expr(ctx, sn->condition);
    AlirBlock *end_bb = alir_add_block(ctx->current_func, "switch_end");
    AlirBlock *default_bb = end_bb; 
    
    if (sn->default_case) default_bb = alir_add_block(ctx->current_func, "switch_default");

    // Create Switch Instruction
    AlirInst *sw = mk_inst(ALIR_OP_SWITCH, NULL, cond, alir_val_label(default_bb->label));
    sw->cases = NULL;
    AlirSwitchCase **tail = &sw->cases;

    // Pre-scan cases to build jump table
    ASTNode *c = sn->cases;
    while(c) {
        CaseNode *cn = (CaseNode*)c;
        AlirBlock *case_bb = alir_add_block(ctx->current_func, "case");
        
        // Add to instruction list
        AlirSwitchCase *sc = calloc(1, sizeof(AlirSwitchCase));
        sc->label = case_bb->label;
        if (cn->value->type == NODE_LITERAL) 
            sc->value = ((LiteralNode*)cn->value)->val.int_val;
        
        *tail = sc;
        tail = &sc->next;
        
        c = c->next;
    }
    emit(ctx, sw); // Emit switch at end of current block

    // Generate Bodies
    c = sn->cases;
    AlirSwitchCase *sc_iter = sw->cases;
    while(c) {
        CaseNode *cn = (CaseNode*)c;
        
        // Find the block we assigned
        AlirBlock *case_bb = NULL;
        AlirBlock *search = ctx->current_func->blocks;
        while(search) { 
            if (strcmp(search->label, sc_iter->label) == 0) { case_bb = search; break; }
            search = search->next;
        }
        
        ctx->current_block = case_bb;
        push_loop(ctx, NULL, end_bb); // Break inside switch goes to end
        
        ASTNode *stmt = cn->body;
        while(stmt) { alir_gen_stmt(ctx, stmt); stmt = stmt->next; }
        
        if (!cn->is_leak) emit(ctx, mk_inst(ALIR_OP_BR, NULL, alir_val_label(end_bb->label), NULL));
        // If leak, we fallthrough to next block naturally (or explicitly BR if blocks reordered)
        
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

static void gen_flux_yield(AlirCtx *ctx, EmitNode *en) {
    AlirValue *val = alir_gen_expr(ctx, en->value);
    
    // In ALIR, we keep yield high-level. 
    // The Backend will lower this to: store state -> return -> resume block
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
        case NODE_SWITCH: gen_switch(ctx, (SwitchNode*)node); break;
        case NODE_EMIT: gen_flux_yield(ctx, (EmitNode*)node); break;
        // ... (If, While, Loop, Return - similar to previous implementation)
        case NODE_RETURN: {
            ReturnNode *rn = (ReturnNode*)node;
            AlirValue *v = rn->value ? alir_gen_expr(ctx, rn->value) : NULL;
            emit(ctx, mk_inst(ALIR_OP_RET, NULL, v, NULL));
            break;
        }
        case NODE_CALL: alir_gen_expr(ctx, node); break;
    }
}

// ... (Rest of function generation logic)

AlirModule* alir_generate(ASTNode *root) {
    AlirCtx ctx;
    memset(&ctx, 0, sizeof(AlirCtx));
    ctx.module = alir_create_module("main_module");
    
    // Pre-pass: Register Classes
    ASTNode *curr = root;
    while(curr) {
        if (curr->type == NODE_CLASS) alir_register_class(&ctx, (ClassNode*)curr);
        curr = curr->next;
    }
    
    // Gen Functions
    curr = root;
    while(curr) {
        if (curr->type == NODE_FUNC_DEF) {
            FuncDefNode *fn = (FuncDefNode*)curr;
            ctx.current_func = alir_add_function(ctx.module, fn->name, fn->ret_type, fn->is_flux);
            ctx.current_block = alir_add_block(ctx.current_func, "entry");
            ctx.temp_counter = 0;
            ctx.symbols = NULL; // Reset symbols for new scope
            
            // Params
            Parameter *p = fn->params;
            while(p) {
                AlirValue *ptr = new_temp(&ctx, p->type);
                emit(&ctx, mk_inst(ALIR_OP_ALLOCA, ptr, NULL, NULL));
                alir_add_symbol(&ctx, p->name, ptr, p->type);
                p = p->next;
            }
            
            ASTNode *stmt = fn->body;
            while(stmt) { alir_gen_stmt(&ctx, stmt); stmt = stmt->next; }
        }
        curr = curr->next;
    }
    return ctx.module;
}
