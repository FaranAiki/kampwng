#include "alir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Loop Stack
void push_loop(AlirCtx *ctx, AlirBlock *cont, AlirBlock *brk) {
    AlirCtx *node = alir_alloc(ctx->module, sizeof(AlirCtx));
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
}

// Helper to check if an instruction is a block terminator
static int is_terminator(AlirOpcode op) {
    return op == ALIR_OP_RET || 
           op == ALIR_OP_JUMP || 
           op == ALIR_OP_CONDI || 
           op == ALIR_OP_SWITCH || 
           op == ALIR_OP_YIELD;
}

// Helper to extract constant integer from AST node (Literals or Enum Members)
long alir_eval_constant_int(AlirCtx *ctx, ASTNode *node) {
    if (!node) return 0;
    
    if (node->type == NODE_LITERAL) {
        return ((LiteralNode*)node)->val.int_val;
    }
    
    // Handle Enum.Member Access
    if (node->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)node;
        VarType obj_t = sem_get_node_type(ctx->sem, ma->object);
        
        if (obj_t.base == TYPE_ENUM && obj_t.class_name) {
            long val = 0;
            if (alir_get_enum_value(ctx->module, obj_t.class_name, ma->member_name, &val)) {
                return val;
            }
        }
    }
    
    // Handle Unary Minus on literals
    if (node->type == NODE_UNARY_OP) {
        UnaryOpNode *u = (UnaryOpNode*)node;
        if (u->op == TOKEN_MINUS) {
            return -alir_eval_constant_int(ctx, u->operand);
        }
    }
    
    return 0; // Fallback / Error
}

static ClassNode* find_class_node(ASTNode *root, const char *name) {
    ASTNode *curr = root;
    while(curr) {
        if (curr->type == NODE_CLASS && strcmp(((ClassNode*)curr)->name, name) == 0) return (ClassNode*)curr;
        if (curr->type == NODE_NAMESPACE) {
            ClassNode *cn = find_class_node(((NamespaceNode*)curr)->body, name);
            if (cn) return cn;
        }
        curr = curr->next;
    }
    return NULL;
}

static void build_struct_fields(AlirCtx *ctx, ASTNode *root, ClassNode *cn, AlirStruct *st) {
    if (st->field_count != -1) return; // Already built
    
    int idx = 0;
    AlirField *head = NULL;
    AlirField **tail = &head;
    
    if (cn->parent_name) {
        AlirStruct *parent_st = alir_find_struct(ctx->module, cn->parent_name);
        if (parent_st) {
            if (parent_st->field_count == -1) {
                ClassNode *pcn = find_class_node(root, cn->parent_name);
                if (pcn) build_struct_fields(ctx, root, pcn, parent_st);
            }
            AlirField *pf = parent_st->fields;
            while(pf) {
                AlirField *nf = alir_alloc(ctx->module, sizeof(AlirField));
                nf->name = alir_strdup(ctx->module, pf->name); 
                nf->type = pf->type;
                nf->index = idx++;
                
                *tail = nf;
                tail = &nf->next;
                pf = pf->next;
            }
        }
    }

    ASTNode *mem = cn->members;
    while(mem) {
        if (mem->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)mem;
            AlirField *f = alir_alloc(ctx->module, sizeof(AlirField));
            f->name = alir_strdup(ctx->module, vd->name);
            f->type = vd->var_type;
            f->index = idx++;
            
            *tail = f;
            tail = &f->next;
        }
        mem = mem->next;
    }
    
    st->fields = head;
    st->field_count = idx;
}

static void pass1_register(AlirCtx *ctx, ASTNode *n) {
    while(n) {
        if (n->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)n;
            alir_register_struct(ctx->module, cn->name, NULL);
        } else if (n->type == NODE_ENUM) {
            EnumNode *en = (EnumNode*)n;
            AlirEnumEntry *head = NULL;
            AlirEnumEntry **tail = &head;
            
            EnumEntry *ent = en->entries;
            while(ent) {
                AlirEnumEntry *ae = alir_alloc(ctx->module, sizeof(AlirEnumEntry));
                ae->name = alir_strdup(ctx->module, ent->name);
                ae->value = ent->value;
                *tail = ae;
                tail = &ae->next;
                ent = ent->next;
            }
            alir_register_enum(ctx->module, en->name, head);
        } else if (n->type == NODE_NAMESPACE) {
            pass1_register(ctx, ((NamespaceNode*)n)->body);
        }
        n = n->next;
    }
}

static void pass2_populate(AlirCtx *ctx, ASTNode *root, ASTNode *n) {
    while(n) {
        if (n->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)n;
            AlirStruct *st = alir_find_struct(ctx->module, cn->name);
            if (st) build_struct_fields(ctx, root, cn, st);
        } else if (n->type == NODE_NAMESPACE) {
            pass2_populate(ctx, root, ((NamespaceNode*)n)->body);
        }
        n = n->next;
    }
}

void alir_scan_and_register_classes(AlirCtx *ctx, ASTNode *root) {
    pass1_register(ctx, root);
    pass2_populate(ctx, root, root);
}


void alir_gen_switch(AlirCtx *ctx, SwitchNode *sn) {
    AlirValue *cond = alir_gen_expr(ctx, sn->condition);
    if (!cond) cond = alir_const_int(ctx->module, 0); // Safety net for unresolvable conditions

    AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "switch_end");
    AlirBlock *default_bb = end_bb; 
    
    if (sn->default_case) default_bb = alir_add_block(ctx->module, ctx->current_func, "switch_default");

    AlirInst *sw = mk_inst(ctx->module, ALIR_OP_SWITCH, NULL, cond, alir_val_label(ctx->module, default_bb->label));
    sw->cases = NULL;
    AlirSwitchCase **tail = &sw->cases;

    ASTNode *c = sn->cases;
    while(c) {
        CaseNode *cn = (CaseNode*)c;
        AlirBlock *case_bb = alir_add_block(ctx->module, ctx->current_func, "case");
        
        AlirSwitchCase *sc = alir_alloc(ctx->module, sizeof(AlirSwitchCase));
        sc->label = case_bb->label;
        sc->value = alir_eval_constant_int(ctx, cn->value);
        
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
        
        if (!cn->is_leak) emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, end_bb->label), NULL));
        
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
        emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, end_bb->label), NULL));
    }
    
    ctx->current_block = end_bb;
}

void alir_gen_stmt(AlirCtx *ctx, ASTNode *node) {
    if (!node) return;
    
    ctx->current_line = node->line;
    ctx->current_col = node->col;

    if (node->type == NODE_VAR_DECL && ctx->in_flux_resume) {
        VarDeclNode *vn = (VarDeclNode*)node;
        FluxVar *fv = ctx->flux_vars;
        while(fv) {
            if (strcmp(fv->name, vn->name) == 0) break;
            fv = fv->next;
        }
        
        if (fv) {
            VarType ptr_type = vn->var_type;
            ptr_type.ptr_depth++;
            AlirValue *ptr = new_temp(ctx, ptr_type);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, fv->index)));
            
            alir_add_symbol(ctx, vn->name, ptr, vn->var_type);
            
            if (vn->initializer) {
                AlirValue *val = alir_gen_expr(ctx, vn->initializer);
                if (!val) val = alir_const_int(ctx->module, 0); // Safety net
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
            }
            return; 
        }
    }

    switch(node->type) {
        case NODE_CLEAN:
        case NODE_WASH:
            break;
        case NODE_VAR_DECL: {
            VarDeclNode *vn = (VarDeclNode*)node;
            AlirValue *ptr = new_temp(ctx, vn->var_type);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
            alir_add_symbol(ctx, vn->name, ptr, vn->var_type);
            if (vn->initializer) {
                AlirValue *val = alir_gen_expr(ctx, vn->initializer);
                if (!val) val = alir_const_int(ctx->module, 0); // Safety net
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
            }
            break;
        }
        case NODE_ASSIGN: {
            AssignNode *an = (AssignNode*)node;
            AlirValue *ptr = NULL;
            
            if (an->target) {
                ptr = alir_gen_addr(ctx, an->target);
                // ... fallback logic remains ...
            } else if (an->name) {
                AlirSymbol *s = alir_find_symbol(ctx, an->name);
                if (s) { 
                    ptr = s->ptr;
                } else {
                    // [FIX] Struct Field as Global Bug: Handle implicit `this.field`
                    AlirSymbol *this_sym = alir_find_symbol(ctx, "this");
                    if (this_sym && this_sym->type.class_name) {
                        int idx = alir_get_field_index(ctx->module, this_sym->type.class_name, an->name);
                        if (idx != -1) {
                            AlirValue *this_ptr = new_temp(ctx, this_sym->type);
                            emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, this_ptr, this_sym->ptr, NULL));
                            ptr = new_temp(ctx, (VarType){TYPE_AUTO, 1, NULL});
                            emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, this_ptr, alir_const_int(ctx->module, idx)));
                        }
                    }
                    if (!ptr) ptr = alir_val_global(ctx->module, an->name, (VarType){TYPE_AUTO,0,NULL}); 
                }
            }
            
            if (!ptr) {
                ptr = new_temp(ctx, (VarType){TYPE_INT, 1, NULL});
                emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
            }
            
            AlirValue *val = alir_gen_expr(ctx, an->value);
            if (!val) val = alir_const_int(ctx->module, 0);
            
            // [FIX] Reconstruct lost Semantic Analyzer typing dynamically
            if (an->name) {
                AlirSymbol *sym = alir_find_symbol(ctx, an->name);
                if (sym && !sym->type.class_name && val->type.class_name) {
                    sym->type.class_name = alir_strdup(ctx->module, val->type.class_name);
                    sym->type.base = val->type.base;
                }
            }
            
            emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
            break;
        }
        case NODE_SWITCH: alir_gen_switch(ctx, (SwitchNode*)node); break;
        case NODE_EMIT: alir_gen_flux_yield(ctx, (EmitNode*)node); break;
        
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode*)node;
            AlirBlock *cond_bb = alir_add_block(ctx->module, ctx->current_func, "while_cond");
            AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "while_body");
            AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "while_end");

            if (wn->is_do_while) {
                emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, body_bb->label), NULL));
                
                ctx->current_block = body_bb;
                push_loop(ctx, cond_bb, end_bb);
                ASTNode *s = wn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
                pop_loop(ctx);
                
                emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));

                ctx->current_block = cond_bb;
                AlirValue *cond = alir_gen_expr(ctx, wn->condition);
                if (!cond) cond = alir_const_int(ctx->module, 0); // Safety net

                AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, cond, alir_val_label(ctx->module, body_bb->label));
                br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
                br->args[0] = alir_val_label(ctx->module, end_bb->label);
                br->arg_count = 1;
                emit(ctx, br);
            } else {
                emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));

                ctx->current_block = cond_bb;
                AlirValue *cond = alir_gen_expr(ctx, wn->condition);
                if (!cond) cond = alir_const_int(ctx->module, 0); // Safety net

                AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, cond, alir_val_label(ctx->module, body_bb->label));
                br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
                br->args[0] = alir_val_label(ctx->module, end_bb->label);
                br->arg_count = 1;
                emit(ctx, br);

                ctx->current_block = body_bb;
                push_loop(ctx, cond_bb, end_bb);
                ASTNode *s = wn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
                pop_loop(ctx);
                
                emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
            }
            ctx->current_block = end_bb;
            break;
        }

        case NODE_LOOP: {
            LoopNode *ln = (LoopNode*)node;
            AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "loop_body");
            AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "loop_end");
            
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, body_bb->label), NULL));
            
            ctx->current_block = body_bb;
            push_loop(ctx, body_bb, end_bb);
            
            ASTNode *s = ln->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
            pop_loop(ctx);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, body_bb->label), NULL));
            
            ctx->current_block = end_bb;
            break;
        }

        case NODE_FOR_IN: {
            ForInNode *fn = (ForInNode*)node;
            AlirValue *col = alir_gen_expr(ctx, fn->collection);
            if (!col) {
                col = new_temp(ctx, (VarType){TYPE_AUTO, 1, NULL, 0, 0});
                emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, col, NULL, NULL));
            }
            
            AlirValue *iter = new_temp(ctx, (VarType){TYPE_VOID, 1, NULL, 0, 0}); 
            emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_INIT, iter, col, NULL));
            
            AlirBlock *cond_bb = alir_add_block(ctx->module, ctx->current_func, "for_cond");
            AlirBlock *body_bb = alir_add_block(ctx->module, ctx->current_func, "for_body");
            AlirBlock *end_bb = alir_add_block(ctx->module, ctx->current_func, "for_end");
            
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
            
            ctx->current_block = cond_bb;
            AlirValue *valid = new_temp(ctx, (VarType){TYPE_BOOL, 0, NULL, 0, 0});
            emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_VALID, valid, iter, NULL));
            
            AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, valid, alir_val_label(ctx->module, body_bb->label));
            br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
            br->args[0] = alir_val_label(ctx->module, end_bb->label);
            br->arg_count = 1;
            emit(ctx, br);
            
            ctx->current_block = body_bb;
            push_loop(ctx, cond_bb, end_bb);
            
            AlirValue *val = new_temp(ctx, fn->iter_type); 
            emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_GET, val, iter, NULL));
            
            if (ctx->in_flux_resume) {
                FluxVar *fv = ctx->flux_vars;
                while(fv) { if(strcmp(fv->name, fn->var_name)==0) break; fv=fv->next; }
                if (fv) {
                    AlirValue *ptr = new_temp(ctx, (VarType){TYPE_INT, 1, NULL, 0, 0}); 
                    emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, fv->index)));
                    alir_add_symbol(ctx, fn->var_name, ptr, fn->iter_type);
                    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, ptr));
                }
            } else {
                AlirValue *var_ptr = new_temp(ctx, fn->iter_type); 
                emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, var_ptr, NULL, NULL));
                alir_add_symbol(ctx, fn->var_name, var_ptr, fn->iter_type);
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, val, var_ptr));
            }
            
            ASTNode *s = fn->body; while(s) { alir_gen_stmt(ctx, s); s=s->next; }
            
            emit(ctx, mk_inst(ctx->module, ALIR_OP_ITER_NEXT, NULL, iter, NULL));
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, cond_bb->label), NULL));
            
            pop_loop(ctx);
            ctx->current_block = end_bb;
            break;
        }

        case NODE_BREAK:
            if (ctx->loop_break) emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, ctx->loop_break->label), NULL));
            break;
            
        case NODE_CONTINUE:
            if (ctx->loop_continue) emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, ctx->loop_continue->label), NULL));
            break;

        case NODE_RETURN: {
            ReturnNode *rn = (ReturnNode*)node;
            if (ctx->in_flux_resume) {
                AlirValue *fin_ptr = new_temp(ctx, (VarType){TYPE_BOOL, 1});
                emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, fin_ptr, ctx->flux_ctx_ptr, alir_const_int(ctx->module, 1))); 
                emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_const_int(ctx->module, 1), fin_ptr));
                emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
            } else {
                AlirValue *v = NULL;
                if (rn->value) {
                    v = alir_gen_expr(ctx, rn->value);
                    if (!v) v = alir_const_int(ctx->module, 0); // Safety net
                }
                emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, v, NULL));
            }
            break;
        }
        
        case NODE_CALL: 
        case NODE_METHOD_CALL:
        case NODE_VAR_REF:
        case NODE_BINARY_OP:
        case NODE_UNARY_OP:
        case NODE_LITERAL:
        case NODE_ARRAY_LIT:
        case NODE_ARRAY_ACCESS:
        case NODE_MEMBER_ACCESS:
        case NODE_TRAIT_ACCESS:
        case NODE_TYPEOF:
        case NODE_HAS_METHOD:
        case NODE_HAS_ATTRIBUTE:
        case NODE_CAST:
        case NODE_INC_DEC:
            alir_gen_expr(ctx, node); 
            break;

        case NODE_IF: {
            IfNode *in = (IfNode*)node;
            AlirValue *cond = alir_gen_expr(ctx, in->condition);
            if (!cond) cond = alir_const_int(ctx->module, 0); // Safety net

            AlirBlock *then_bb = alir_add_block(ctx->module, ctx->current_func, "then");
            AlirBlock *else_bb = alir_add_block(ctx->module, ctx->current_func, "else");
            AlirBlock *merge_bb = alir_add_block(ctx->module, ctx->current_func, "merge");
            
            AlirBlock *target_else = in->else_body ? else_bb : merge_bb;
            
            AlirInst *br = mk_inst(ctx->module, ALIR_OP_CONDI, NULL, cond, alir_val_label(ctx->module, then_bb->label));
            br->args = alir_alloc(ctx->module, sizeof(AlirValue*));
            br->args[0] = alir_val_label(ctx->module, target_else->label);
            br->arg_count = 1;
            emit(ctx, br);
            
            ctx->current_block = then_bb;
            ASTNode *s = in->then_body; while(s){ alir_gen_stmt(ctx,s); s=s->next; }
            emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, merge_bb->label), NULL));
            
            if (in->else_body) {
                ctx->current_block = else_bb;
                s = in->else_body; while(s){ alir_gen_stmt(ctx,s); s=s->next; }
                emit(ctx, mk_inst(ctx->module, ALIR_OP_JUMP, NULL, alir_val_label(ctx->module, merge_bb->label), NULL));
            }
            
            ctx->current_block = merge_bb;
            break;
        }

        case NODE_ROOT:
        case NODE_FUNC_DEF:
        case NODE_CLASS:
        case NODE_NAMESPACE:
        case NODE_ENUM:
        case NODE_LINK:
        case NODE_CASE:
            break;
    }
}

// Generate an implicit Default Constructor for Classes
// [FIX] Update Implicit Constructor to map fields correctly
void alir_gen_implicit_constructor(AlirCtx *ctx, ClassNode *cn) {
    ctx->current_func = alir_add_function(ctx->module, cn->name, (VarType){TYPE_VOID, 0}, 0);
    
    VarType this_t = {TYPE_CLASS, 1, alir_strdup(ctx->module, cn->name)};
    alir_func_add_param(ctx->module, ctx->current_func, "this", this_t);

    // Expand signature to match all fields initialized by caller
    AlirStruct *st = alir_find_struct(ctx->module, cn->name);
    if (st) {
        AlirField *f = st->fields;
        while(f) {
            alir_func_add_param(ctx->module, ctx->current_func, f->name, f->type);
            f = f->next;
        }
    }

    ctx->current_block = alir_add_block(ctx->module, ctx->current_func, "entry");
    
    // Bind 'this' pointer
    AlirValue *this_ptr = new_temp(ctx, this_t);
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, this_ptr, NULL, NULL));
    alir_add_symbol(ctx, "this", this_ptr, this_t);
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_val_var(ctx->module, "p0"), this_ptr));

    // Map passed arguments sequentially to struct fields
    if (st) {
        AlirField *f = st->fields;
        int p_idx = 1;
        while(f) {
            char pname[16]; snprintf(pname, 16, "p%d", p_idx++);
            AlirValue *arg_val = alir_val_var(ctx->module, pname);
            
            AlirValue *loaded_this = new_temp(ctx, this_t);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_LOAD, loaded_this, this_ptr, NULL));

            VarType ft = f->type; ft.ptr_depth++;
            AlirValue *field_ptr = new_temp(ctx, ft);
            emit(ctx, mk_inst(ctx->module, ALIR_OP_GET_PTR, field_ptr, loaded_this, alir_const_int(ctx->module, f->index)));
            emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, arg_val, field_ptr));
            
            f = f->next;
        }
    }

    emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
}

// Generate the definition of a standard Function or Class Method
void alir_gen_function_def(AlirCtx *ctx, FuncDefNode *fn, const char *class_name) {
    if (fn->is_flux) {
        alir_gen_flux_def(ctx, fn);
        return;
    }

    char func_name[256];
    if (class_name) {
        // Intercept methods inside Class scope. `init` -> `@ClassName`. Else `ClassName_MethodName`
        if (strcmp(fn->name, "init") == 0 || strcmp(fn->name, class_name) == 0) {
            snprintf(func_name, sizeof(func_name), "%s", class_name);
        } else {
            snprintf(func_name, sizeof(func_name), "%s_%s", class_name, fn->name);
        }
    } else {
        snprintf(func_name, sizeof(func_name), "%s", fn->name);
    }

    ctx->current_func = alir_add_function(ctx->module, func_name, fn->ret_type, 0);

    // 1. Setup 'this' pointer as the primary parameter if it's a method/constructor
    if (class_name) {
        VarType this_t = {TYPE_CLASS, 1, alir_strdup(ctx->module, class_name)};
        alir_func_add_param(ctx->module, ctx->current_func, "this", this_t);
    }

    // 2. Setup user-defined explicit parameters
    Parameter *p = fn->params;
    while(p) {
        alir_func_add_param(ctx->module, ctx->current_func, p->name, p->type);
        p = p->next;
    }

    if (!fn->body) return;

    ctx->current_block = alir_add_block(ctx->module, ctx->current_func, "entry");
    ctx->temp_counter = 0;
    ctx->symbols = NULL; 

    int p_idx = 0;

    if (class_name) {
        VarType this_t = {TYPE_CLASS, 1, alir_strdup(ctx->module, class_name)};
        AlirValue *ptr = new_temp(ctx, this_t);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
        alir_add_symbol(ctx, "this", ptr, this_t);

        char pname[16]; snprintf(pname, sizeof(pname), "p%d", p_idx++);
        AlirValue *pval = alir_val_var(ctx->module, pname);
        pval->type = this_t; // [FIX] explicitly attach type for STORE
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, pval, ptr));
    }

    // Map explicit parameters
    p = fn->params;
    while(p) {
        AlirValue *ptr = new_temp(ctx, p->type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
        alir_add_symbol(ctx, p->name, ptr, p->type);
        
        char pname[16]; snprintf(pname, sizeof(pname), "p%d", p_idx++);
        AlirValue *pval = alir_val_var(ctx->module, pname); 
        pval->type = p->type; // [FIX] explicitly attach type for STORE
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, pval, ptr));
        
        p = p->next;
    }
    
    ASTNode *stmt = fn->body;
    while(stmt) { alir_gen_stmt(ctx, stmt); stmt = stmt->next; }

    // Enforce implicit block termination (e.g. adding `return 0` to main or void blocks)
    if (ctx->current_block) {
        AlirInst *tail = ctx->current_block->tail;
        int has_term = tail && is_terminator(tail->op);
        
        if (!has_term) {
            ctx->current_line = fn->base.line;
            ctx->current_col = fn->base.col;
            
            if (strcmp(func_name, "main") == 0) {
                emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, alir_const_int(ctx->module, 0), NULL));
            } else if (fn->ret_type.base == TYPE_VOID || (class_name && (strcmp(fn->name, "init") == 0 || strcmp(fn->name, class_name) == 0))) {
                emit(ctx, mk_inst(ctx->module, ALIR_OP_RET, NULL, NULL, NULL));
            }
        }
    }
}

// Deeply scan AST for Class/Methods and Standard Functions
void alir_gen_functions_recursive(AlirCtx *ctx, ASTNode *root) {
    ASTNode *curr = root;
    while(curr) {
        if (curr->type == NODE_FUNC_DEF) {
            alir_gen_function_def(ctx, (FuncDefNode*)curr, NULL);
        } else if (curr->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)curr;
            int has_constructor = 0;
            
            ASTNode *mem = cn->members;
            while(mem) {
                if (mem->type == NODE_FUNC_DEF) {
                    FuncDefNode *fn = (FuncDefNode*)mem;
                    if (strcmp(fn->name, cn->name) == 0 || strcmp(fn->name, "init") == 0) {
                        has_constructor = 1;
                    }
                    alir_gen_function_def(ctx, fn, cn->name);
                }
                mem = mem->next;
            }
            
            // Emit an implicit constructor if the user hasn't explicitly supplied `init`
            if (!has_constructor) {
                alir_gen_implicit_constructor(ctx, cn);
            }
        } else if (curr->type == NODE_NAMESPACE) {
            alir_gen_functions_recursive(ctx, ((NamespaceNode*)curr)->body);
        }
        curr = curr->next;
    }
}

AlirModule* alir_generate(SemanticCtx *sem, ASTNode *root) {
    AlirCtx ctx;
    memset(&ctx, 0, sizeof(AlirCtx));
    ctx.sem = sem; 
    ctx.module = alir_create_module(sem ? sem->compiler_ctx : NULL, "main_module");

    if (sem) {
        ctx.module->src = sem->current_source;
        ctx.module->filename = sem->current_filename;
    }
    
    // 1. SCAN AND REGISTER CLASSES & ENUMS
    alir_scan_and_register_classes(&ctx, root);
    
    // 2. GEN FUNCTIONS (Recursively to handle classes & namespaces)
    alir_gen_functions_recursive(&ctx, root);
    
    return ctx.module;
}
