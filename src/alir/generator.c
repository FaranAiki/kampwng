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
int is_terminator(AlirOpcode op) {
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

ClassNode* find_class_node(ASTNode *root, const char *name) {
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

void build_struct_fields(AlirCtx *ctx, ASTNode *root, ClassNode *cn, AlirStruct *st) {
    if (st->field_count != -1) return; // Already built
    
    int idx = 0;
    AlirField *head = NULL;
    AlirField **tail = &head;
    
    // 1. Inherit Fields from Parent Class
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
    
    // 2. Inherit Fields from Traits
    for (int i = 0; i < cn->traits.count; i++) {
        AlirStruct *trait_st = alir_find_struct(ctx->module, cn->traits.names[i]);
        if (trait_st) {
            if (trait_st->field_count == -1) {
                ClassNode *tcn = find_class_node(root, cn->traits.names[i]);
                if (tcn) build_struct_fields(ctx, root, tcn, trait_st);
            }
            AlirField *tf = trait_st->fields;
            while(tf) {
                AlirField *nf = alir_alloc(ctx->module, sizeof(AlirField));
                nf->name = alir_strdup(ctx->module, tf->name);
                nf->type = tf->type;
                nf->index = idx++;
                
                *tail = nf;
                tail = &nf->next;
                tf = tf->next;
            }
        }
    }

    // 3. Current Class Fields
    ASTNode *mem = cn->members;
    while(mem) {
        if (mem->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)mem;
            AlirField *f = alir_alloc(ctx->module, sizeof(AlirField));
            f->name = alir_strdup(ctx->module, vd->name);
            f->type = vd->var_type;
            
            // [FIX] Decay inline arrays to pointers to prevent struct bloat and truncation crashes
            if (f->type.array_size > 0) {
                f->type.array_size = 0;
                f->type.ptr_depth++;
            }
            
            f->index = idx++;
            
            *tail = f;
            tail = &f->next;
        }
        mem = mem->next;
    }
    
    st->fields = head;
    st->field_count = idx;
}

void pass1_register(AlirCtx *ctx, ASTNode *n) {
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

void pass2_populate(AlirCtx *ctx, ASTNode *root, ASTNode *n) {
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

void alir_gen_implicit_constructor(AlirCtx *ctx, ClassNode *cn) {
    ctx->current_func = alir_add_function(ctx->module, cn->name, (VarType){TYPE_VOID, 0}, 0);
    
    VarType this_t = {TYPE_CLASS, 1, alir_strdup(ctx->module, cn->name)};
    alir_func_add_param(ctx->module, ctx->current_func, "this", this_t);

    AlirStruct *st = alir_find_struct(ctx->module, cn->name);
    if (st) {
        AlirField *f = st->fields;
        while(f) {
            alir_func_add_param(ctx->module, ctx->current_func, f->name, f->type);
            f = f->next;
        }
    }

    ctx->current_block = alir_add_block(ctx->module, ctx->current_func, "entry");
    
    AlirValue *this_ptr = new_temp(ctx, this_t);
    emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, this_ptr, NULL, NULL));
    alir_add_symbol(ctx, "this", this_ptr, this_t);
    emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, alir_val_var(ctx->module, "p0"), this_ptr));

    if (st) {
        AlirField *f = st->fields;
        int p_idx = 1;
        while(f) {
            char pname[16]; snprintf(pname, 16, "p%d", p_idx++);
            AlirValue *arg_val = alir_val_var(ctx->module, pname);
            arg_val->type = f->type; // [FIX] Attach type to prevent untyped store
            
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

void alir_gen_function_def(AlirCtx *ctx, FuncDefNode *fn, const char *class_name) {
    if (fn->is_flux) {
        alir_gen_flux_def(ctx, fn);
        return;
    }

    char func_name[256];
    if (class_name) {
        if (strcmp(fn->name, "init") == 0 || strcmp(fn->name, class_name) == 0) {
            snprintf(func_name, sizeof(func_name), "%s", class_name);
        } else {
            snprintf(func_name, sizeof(func_name), "%s_%s", class_name, fn->name);
        }
    } else {
        snprintf(func_name, sizeof(func_name), "%s", fn->name);
    }

    ctx->current_func = alir_add_function(ctx->module, func_name, fn->ret_type, 0);
    ctx->current_func->is_varargs = fn->is_varargs;

    if (class_name) {
        VarType this_t = {TYPE_CLASS, 1, alir_strdup(ctx->module, class_name)};
        alir_func_add_param(ctx->module, ctx->current_func, "this", this_t);
    }

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
        
        char pname[16]; snprintf(pname, sizeof(pname), "p%d", p_idx++);
        AlirValue *pval = alir_val_var(ctx->module, pname);
        pval->type = this_t;
        
        // [FIX] Alias 'this' directly to the argument instead of re-allocating.
        // Because method calls pass the object's lvalue (Class**), this allows 
        // the implicit AST LOAD to automatically dereference down to Class* safely.
        alir_add_symbol(ctx, "this", pval, this_t);
    }

    p = fn->params;
    while(p) {
        AlirValue *ptr = new_temp(ctx, p->type);
        emit(ctx, mk_inst(ctx->module, ALIR_OP_ALLOCA, ptr, NULL, NULL));
        alir_add_symbol(ctx, p->name, ptr, p->type);
        
        char pname[16]; snprintf(pname, sizeof(pname), "p%d", p_idx++);
        AlirValue *pval = alir_val_var(ctx->module, pname); 
        pval->type = p->type;
        emit(ctx, mk_inst(ctx->module, ALIR_OP_STORE, NULL, pval, ptr));
        
        p = p->next;
    }
    
    ASTNode *stmt = fn->body;
    while(stmt) { alir_gen_stmt(ctx, stmt); stmt = stmt->next; }

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

// Emits inherited methods from parent and traits down to the derived class scope
void alir_gen_inherited_methods(AlirCtx *ctx, ASTNode *root, ClassNode *cn, const char *target_class) {
    if (!cn) return;
    
    // 1. Traverse Parent
    if (cn->parent_name) {
        ClassNode *pcn = find_class_node(root, cn->parent_name);
        if (pcn) {
            alir_gen_inherited_methods(ctx, root, pcn, target_class); // Deepest first
            
            ASTNode *mem = pcn->members;
            while (mem) {
                if (mem->type == NODE_FUNC_DEF) {
                    FuncDefNode *fn = (FuncDefNode*)mem;
                    if (strcmp(fn->name, pcn->name) != 0 && strcmp(fn->name, "init") != 0) {
                        ClassNode *tcn = find_class_node(root, target_class);
                        int is_overridden = 0;
                        if (tcn) {
                            ASTNode *tmem = tcn->members;
                            while(tmem) {
                                if (tmem->type == NODE_FUNC_DEF && strcmp(((FuncDefNode*)tmem)->name, fn->name) == 0) {
                                    is_overridden = 1; break;
                                }
                                tmem = tmem->next;
                            }
                        }
                        if (!is_overridden) {
                            alir_gen_function_def(ctx, fn, target_class);
                        }
                    }
                }
                mem = mem->next;
            }
        }
    }
    
    // 2. Traverse Traits
    for (int i = 0; i < cn->traits.count; i++) {
        ClassNode *tcn = find_class_node(root, cn->traits.names[i]);
        if (tcn) {
            alir_gen_inherited_methods(ctx, root, tcn, target_class);
            
            ASTNode *mem = tcn->members;
            while (mem) {
                if (mem->type == NODE_FUNC_DEF) {
                    FuncDefNode *fn = (FuncDefNode*)mem;
                    if (strcmp(fn->name, tcn->name) != 0 && strcmp(fn->name, "init") != 0) {
                        ClassNode *target_node = find_class_node(root, target_class);
                        int is_overridden = 0;
                        if (target_node) {
                            ASTNode *tmem = target_node->members;
                            while(tmem) {
                                if (tmem->type == NODE_FUNC_DEF && strcmp(((FuncDefNode*)tmem)->name, fn->name) == 0) {
                                    is_overridden = 1; break;
                                }
                                tmem = tmem->next;
                            }
                        }
                        if (!is_overridden) {
                            alir_gen_function_def(ctx, fn, target_class);
                        }
                    }
                }
                mem = mem->next;
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

            // Generate Inherited and Traited Methods for this specific Class
            alir_gen_inherited_methods(ctx, root, cn, cn->name);
            
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
