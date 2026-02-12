#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

VarType resolve_typedef(SemCtx *ctx, VarType t) {
    // We resolve custom types (aliases) recursively
    if (t.class_name) {
        SemTypedef *td = ctx->typedefs;
        while (td) {
            if (strcmp(td->name, t.class_name) == 0) {
                VarType resolved = resolve_typedef(ctx, td->target_type);
                // Inherit pointer depth and array properties from the alias usage
                resolved.ptr_depth += t.ptr_depth;
                if (t.array_size > 0) {
                    resolved.array_size = t.array_size;
                }
                return resolved;
            }
            td = td->next;
        }
    }
    return t;
}

void scan_declarations(SemCtx *ctx, ASTNode *node, const char *prefix) {
    while(node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            char *name = fd->name ? fd->name : "anonymous";
            char *lookup_name = name; 
            char *qualified = NULL;
            
            if (prefix && fd->name) {
                int len = strlen(prefix) + strlen(name) + 2;
                qualified = malloc(len);
                snprintf(qualified, len, "%s.%s", prefix, name);
                lookup_name = qualified;
            }
            
            char *mangled = NULL;
            if (fd->body == NULL) {
                // EXTERN function - Use original name for LLVM linking (FFI)
                // This ensures math.sin still links to C's 'sin'
                mangled = strdup(fd->name ? fd->name : "anonymous_extern");
            } else {
                // Regular function - Full mangling
                mangled = mangle_function(lookup_name, fd->params);
            }
            
            if (mangled) {
                fd->mangled_name = strdup(mangled);
            }
            
            // Collect param types for resolution
            int pcount = 0;
            Parameter *p = fd->params;
            while(p) { pcount++; p = p->next; }
            
            VarType *ptypes = NULL;
            if (pcount > 0) {
                ptypes = malloc(sizeof(VarType) * pcount);
                p = fd->params;
                int i = 0;
                while(p) { ptypes[i++] = p->type; p = p->next; }
            }
            
            // Check redefinition (checking mangled name for overloads)
            if (mangled) {
                SemFunc *exist = ctx->functions;
                while(exist) {
                    // Conflict if same mangled name AND (same lookup name OR not extern)
                    if (exist->mangled_name && strcmp(exist->mangled_name, mangled) == 0) {
                         if (fd->body != NULL || (exist->name && strcmp(exist->name, lookup_name) == 0)) {
                             sem_error(ctx, node, "Redefinition of function '%s' with same signature", lookup_name);
                         }
                    }
                    exist = exist->next;
                }
                
                add_func(ctx, lookup_name, mangled, fd->ret_type, ptypes, pcount);
                free(mangled);
            }
            
            if (qualified) free(qualified);
        } 
        else if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            char *name = cn->name ? cn->name : "anonymous_class";
            char *qualified = NULL;
            if (prefix && cn->name) {
                int len = strlen(prefix) + strlen(name) + 2;
                qualified = malloc(len);
                snprintf(qualified, len, "%s.%s", prefix, name);
                name = qualified;
            }
            
            add_class(ctx, name, cn->parent_name, cn->traits.names, cn->traits.count);
            
            SemClass *cls = find_sem_class(ctx, name);
            if (cls) {
                cls->is_extern = cn->is_extern;
                
                if (!cn->is_extern) {
                    ASTNode *mem = cn->members;
                    while(mem) {
                        if (mem->type == NODE_VAR_DECL) {
                            VarDeclNode *vd = (VarDeclNode*)mem;
                            if (vd->name) {
                                SemSymbol *s = malloc(sizeof(SemSymbol));
                                s->name = strdup(vd->name);
                                s->type = vd->var_type;
                                s->is_mutable = vd->is_mutable;
                                s->is_array = vd->is_array;
                                
                                // Set Decl Location
                                s->decl_line = vd->base.line;
                                s->decl_col = vd->base.col;

                                s->next = cls->members;
                                cls->members = s;
                            }
                        }
                        mem = mem->next;
                    }
                }
            }

            scan_declarations(ctx, cn->members, name);

            if (qualified) free(qualified);
        }
        else if (node->type == NODE_NAMESPACE) {
             NamespaceNode *ns = (NamespaceNode*)node;
             char *new_prefix = ns->name ? ns->name : "anonymous_ns";
             char *qualified = NULL;
             if (prefix && ns->name) {
                 int len = strlen(prefix) + strlen(ns->name) + 2;
                 qualified = malloc(len);
                 snprintf(qualified, len, "%s.%s", prefix, ns->name);
                 new_prefix = qualified;
             }
             scan_declarations(ctx, ns->body, new_prefix);
             if (qualified) free(qualified);
        }
        else if (node->type == NODE_ENUM) {
            EnumNode *en = (EnumNode*)node;
            char *name = en->name ? en->name : "anonymous_enum";
            
            SemEnum *se = malloc(sizeof(SemEnum));
            se->name = strdup(name);
            se->members = NULL;
            se->next = ctx->enums;
            ctx->enums = se;

            EnumEntry *ent = en->entries;
            struct SemEnumMember **tail = &se->members;
            
            while(ent) {
                if (ent->name) {
                    struct SemEnumMember *m = malloc(sizeof(struct SemEnumMember));
                    m->name = strdup(ent->name);
                    m->next = NULL;
                    *tail = m;
                    tail = &m->next;

                    VarType vt = {TYPE_INT, 0, NULL, 0, 0};
                    add_symbol_semantic(ctx, ent->name, vt, 0, 0, 0, en->base.line, en->base.col);
                }
                ent = ent->next;
            }
        }

        node = node->next;
    }
}

void check_program(SemCtx *ctx, ASTNode *node) {
    while(node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            ctx->current_func_ret_type = fd->ret_type;
            enter_scope(ctx);
            
            Parameter *p = fd->params;
            while(p) {
                if (p->name) {
                    // Check shadowing for parameters
                    SemSymbol *shadowed = NULL;
                    Scope *s = ctx->current_scope->parent;
                    
                    // 1. Check outer scopes (globals)
                    while (s) {
                        SemSymbol *sym = s->symbols;
                        while (sym) {
                            if (sym->name && strcmp(sym->name, p->name) == 0) {
                                shadowed = sym;
                                break;
                            }
                            sym = sym->next;
                        }
                        if (shadowed) break;
                        s = s->parent;
                    }
                    
                    // 2. Check class members if inside method
                    if (!shadowed && fd->class_name) {
                        SemSymbol *mem = find_member(ctx, fd->class_name, p->name);
                        if (mem) shadowed = mem;
                    }

                    if (shadowed) {
                         char msg[256];
                         snprintf(msg, 256, "Parameter '%s' shadows a variable in outer scope", p->name);
                         sem_info(ctx, node, msg);
                         if (shadowed->decl_line > 0)
                            sem_reason(ctx, shadowed->decl_line, shadowed->decl_col, "Shadowed declaration is here");
                    }

                    add_symbol_semantic(ctx, p->name, p->type, 1, 0, 0, 0, 0); 
                }
                p = p->next;
            }
            
            if (fd->class_name) {
                 ctx->current_class = fd->class_name;
            }

            check_block(ctx, fd->body);
            
            ctx->current_class = NULL;
            exit_scope(ctx);
        }
        else if (node->type == NODE_VAR_DECL) {
             check_stmt(ctx, node); 
        }
        else if (node->type == NODE_NAMESPACE) {
             check_program(ctx, ((NamespaceNode*)node)->body);
        }
        else if (node->type == NODE_CLASS) {
             ClassNode *cn = (ClassNode*)node;
             ASTNode *m = cn->members;
             // MECE Fix: Ensure Class Bodies are rigorously checked!
             while(m) {
                 if (m->type == NODE_FUNC_DEF) {
                     check_program(ctx, m); // Checks the method
                 }
                 else if (m->type == NODE_VAR_DECL) {
                     check_stmt(ctx, m); // Validates defaults on initializers
                 }
                 m = m->next;
             }
        }
        else {
            check_stmt(ctx, node);
        }
        node = node->next;
    }
}

int semantic_analysis(ASTNode *root, const char *source, const char *filename) {
    SemCtx ctx;
    ctx.current_scope = NULL;
    ctx.functions = NULL;
    ctx.classes = NULL;
    ctx.enums = NULL; 
    ctx.error_count = 0;
    ctx.in_loop = 0;
    ctx.current_class = NULL;
    ctx.source_code = source;
    ctx.filename = filename;
    
    enter_scope(&ctx);
    
    scan_declarations(&ctx, root, NULL);
    check_program(&ctx, root);
    
    exit_scope(&ctx);
    
    return ctx.error_count;
}
