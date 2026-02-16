#include "codegen.h"
#include "../diagnostic/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void codegen_init_ctx(CodegenCtx *ctx, LLVMModuleRef module, LLVMBuilderRef builder, const char *source) {
    ctx->module = module;
    ctx->builder = builder;
    ctx->symbols = NULL;
    ctx->functions = NULL;
    ctx->classes = NULL;
    ctx->enums = NULL; 
    ctx->current_loop = NULL;
    ctx->source_code = source;
    
    // Namespace init
    ctx->current_prefix = NULL;
    ctx->known_namespaces = NULL;
    ctx->known_namespace_count = 0;

    // Flux init (State Machine)
    ctx->in_flux_resume = 0;
    ctx->flux_vars = NULL;
    ctx->flux_struct_type = NULL;
    ctx->flux_ctx_ptr = NULL;
    ctx->flux_resume_switch = NULL;
    ctx->flux_yield_count = 0;

    // Printf
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->printf_type = LLVMFunctionType(LLVMInt32Type(), printf_args, 1, true);
    ctx->printf_func = LLVMAddFunction(module, "printf", ctx->printf_type);
    
    // Malloc
    LLVMTypeRef malloc_args[] = { LLVMInt64Type() };
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), malloc_args, 1, false);
    ctx->malloc_func = LLVMAddFunction(module, "malloc", malloc_type);

    // Calloc
    LLVMTypeRef calloc_args[] = { LLVMInt64Type(), LLVMInt64Type() };
    LLVMTypeRef calloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), calloc_args, 2, false);
    ctx->calloc_func = LLVMAddFunction(module, "calloc", calloc_type);

    // Free
    LLVMTypeRef free_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidType(), free_args, 1, false);
    ctx->free_func = LLVMAddFunction(module, "free", free_type);
    
    // setjmp
    LLVMTypeRef setjmp_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef setjmp_type = LLVMFunctionType(LLVMInt32Type(), setjmp_args, 1, false);
    ctx->setjmp_func = LLVMAddFunction(module, "setjmp", setjmp_type);
    
    // longjmp
    LLVMTypeRef longjmp_args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMInt32Type() };
    LLVMTypeRef longjmp_type = LLVMFunctionType(LLVMVoidType(), longjmp_args, 2, false);
    ctx->longjmp_func = LLVMAddFunction(module, "longjmp", longjmp_type);
    
    // Getchar
    LLVMTypeRef getchar_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
    LLVMValueRef getchar_func = LLVMAddFunction(module, "getchar", getchar_type);
    
    // Strcmp
    LLVMTypeRef strcmp_args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32Type(), strcmp_args, 2, false);
    ctx->strcmp_func = LLVMAddFunction(module, "strcmp", strcmp_type);

    // String Helpers
    ctx->strlen_func = generate_strlen(module);
    ctx->strcpy_func = generate_strcpy(module);
    ctx->strdup_func = generate_strdup(module, ctx->malloc_func, ctx->strlen_func, ctx->strcpy_func);

    // Input
    ctx->input_func = generate_input_func(module, builder, ctx->malloc_func, getchar_func);
}

void codegen_error(CodegenCtx *ctx, ASTNode *node, const char *msg) {
    if (ctx->source_code && node) {
        Lexer l;
        lexer_init(&l, ctx->source_code);
        Token t;
        t.type = TOKEN_UNKNOWN;
        t.line = node->line;
        t.col = node->col;
        t.text = NULL;
        t.int_val = 0; t.double_val = 0;
        report_error(&l, t, msg);
    } else {
        fprintf(stderr, "Error: %s\n", msg);
    }
    exit(1);
}

// ... Symbol management functions ...
void add_symbol(CodegenCtx *ctx, const char *name, LLVMValueRef val, LLVMTypeRef type, VarType vtype, int is_array, int is_mut) {
  Symbol *s = malloc(sizeof(Symbol));
  s->name = strdup(name);
  s->value = val;
  s->type = type;
  s->vtype = vtype;
  s->is_array = is_array;
  s->is_mutable = is_mut;
  s->is_direct_value = 0; // Default to variable
  s->next = ctx->symbols;
  ctx->symbols = s;
}

Symbol* find_symbol(CodegenCtx *ctx, const char *name) {
  Symbol *curr = ctx->symbols;
  while (curr) {
    if (strcmp(curr->name, name) == 0) return curr;
    curr = curr->next;
  }
  return NULL;
}

void add_func_symbol(CodegenCtx *ctx, const char *name, VarType ret_type, VarType *params, int pcount, int is_flux) {
    FuncSymbol *s = malloc(sizeof(FuncSymbol));
    s->name = strdup(name);
    s->ret_type = ret_type;
    s->param_types = params;
    s->param_count = pcount;
    s->is_flux = is_flux;
    s->yield_type = (VarType){0}; 
    s->next = ctx->functions;
    ctx->functions = s;
}

FuncSymbol* find_func_symbol(CodegenCtx *ctx, const char *name) {
    FuncSymbol *curr = ctx->functions;
    while(curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

void add_class_info(CodegenCtx *ctx, ClassInfo *ci) {
    ci->next = ctx->classes;
    ctx->classes = ci;
}

ClassInfo* find_class(CodegenCtx *ctx, const char *name) {
    if (!ctx) return NULL; 
    ClassInfo *cur = ctx->classes;
    while(cur) {
        if (strcmp(cur->name, name) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

void add_enum_info(CodegenCtx *ctx, EnumInfo *ei) {
    ei->next = ctx->enums;
    ctx->enums = ei;
}

EnumInfo* find_enum(CodegenCtx *ctx, const char *name) {
    EnumInfo *cur = ctx->enums;
    while(cur) {
        if (strcmp(cur->name, name) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

void add_namespace_name(CodegenCtx *ctx, const char *name) {
    if (is_namespace(ctx, name)) return;
    ctx->known_namespace_count++;
    ctx->known_namespaces = realloc(ctx->known_namespaces, sizeof(char*) * ctx->known_namespace_count);
    ctx->known_namespaces[ctx->known_namespace_count-1] = strdup(name);
}

int is_namespace(CodegenCtx *ctx, const char *name) {
    for(int i=0; i<ctx->known_namespace_count; i++) {
        if (strcmp(ctx->known_namespaces[i], name) == 0) return 1;
    }
    return 0;
}

int get_member_index(ClassInfo *ci, const char *member, LLVMTypeRef *out_type, VarType *out_vtype) {
    ClassMember *m = ci->members;
    while(m) {
        if (strcmp(m->name, member) == 0) {
            if (out_type) *out_type = m->type;
            if (out_vtype) *out_vtype = m->vtype;
            return m->index;
        }
        m = m->next;
    }
    return -1;
}

int get_trait_offset(CodegenCtx *ctx, ClassInfo *ci, const char *trait_name) {
    TraitOffset *to = ci->trait_offsets;
    while(to) {
        if (strcmp(to->trait_name, trait_name) == 0) return to->offset_index;
        to = to->next;
    }
    if (ci->parent_name) {
        ClassInfo *parent = find_class(ctx, ci->parent_name); 
        if (parent) return get_trait_offset(ctx, parent, trait_name);
    }
    return -1;
}

LLVMTypeRef get_llvm_type(CodegenCtx *ctx, VarType t) {
  if (t.is_func_ptr) {
      LLVMTypeRef ret_t = get_llvm_type(ctx, *t.fp_ret_type);
      LLVMTypeRef *param_types = NULL;
      if (t.fp_param_count > 0) {
          param_types = malloc(sizeof(LLVMTypeRef) * t.fp_param_count);
          for(int i=0; i<t.fp_param_count; i++) {
              param_types[i] = get_llvm_type(ctx, t.fp_param_types[i]);
          }
      }
      LLVMTypeRef func_type = LLVMFunctionType(ret_t, param_types, t.fp_param_count, t.fp_is_varargs);
      if (param_types) free(param_types);
      return LLVMPointerType(func_type, 0); 
  }

  LLVMTypeRef base_type;
  switch (t.base) {
    case TYPE_INT: base_type = LLVMInt32Type(); break;
    case TYPE_SHORT: base_type = LLVMInt16Type(); break;
    case TYPE_LONG: base_type = LLVMInt64Type(); break;
    case TYPE_LONG_LONG: base_type = LLVMInt64Type(); break;
    case TYPE_CHAR: base_type = LLVMInt8Type(); break;
    case TYPE_BOOL: base_type = LLVMInt1Type(); break;
    case TYPE_FLOAT: base_type = LLVMFloatType(); break;
    case TYPE_DOUBLE: base_type = LLVMDoubleType(); break;
    case TYPE_LONG_DOUBLE: base_type = LLVMFP128Type(); break;
    case TYPE_VOID: base_type = LLVMVoidType(); break;
    case TYPE_STRING: base_type = LLVMPointerType(LLVMInt8Type(), 0); break;
    case TYPE_CLASS: {
        if (!t.class_name) return LLVMInt32Type(); 
        ClassInfo *ci = find_class(ctx, t.class_name);
        if (ci) base_type = ci->struct_type;
        else base_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), t.class_name);
        break;
    }
    default: base_type = LLVMInt32Type(); break;
  }
  for (int i=0; i<t.ptr_depth; i++) base_type = LLVMPointerType(base_type, 0);
  
  if (t.array_size > 0) {
      base_type = LLVMArrayType(base_type, t.array_size);
  }
  
  return base_type;
}

// ... Rest of core helpers (scan_classes, etc.) ...
// Note: Keeping existing scan helpers, they are fine.

ClassMember** append_member(CodegenCtx *ctx, ClassInfo *ci, ClassMember **tail, int *idx, const char *name, VarType vt, LLVMTypeRef lt, ASTNode *init) {
    ClassMember *cm = malloc(sizeof(ClassMember));
    cm->name = strdup(name);
    cm->vtype = vt;
    cm->type = lt;
    cm->index = (*idx)++;
    cm->init_expr = init; 
    cm->next = NULL;
    *tail = cm;
    return &cm->next;
}

void get_type_size_align(CodegenCtx *ctx, VarType t, size_t *out_size, size_t *out_align) {
    size_t s = 0, a = 1;
    if (t.ptr_depth > 0 || t.is_func_ptr) { s = 8; a = 8; } 
    else {
        switch (t.base) {
            case TYPE_INT: s = 4; a = 4; break;
            case TYPE_SHORT: s = 2; a = 2; break;
            case TYPE_LONG: s = 8; a = 8; break;
            case TYPE_LONG_LONG: s = 8; a = 8; break;
            case TYPE_CHAR: s = 1; a = 1; break;
            case TYPE_BOOL: s = 1; a = 1; break;
            case TYPE_FLOAT: s = 4; a = 4; break;
            case TYPE_DOUBLE: s = 8; a = 8; break;
            case TYPE_LONG_DOUBLE: s = 16; a = 16; break;
            case TYPE_STRING: s = 8; a = 8; break;
            case TYPE_CLASS: {
                if(t.class_name) {
                    ClassInfo *ci = find_class(ctx, t.class_name);
                    if(ci) {
                        size_t struct_size = 0, struct_align = 1;
                        ClassMember *m = ci->members;
                        while(m) {
                            size_t ms, ma;
                            get_type_size_align(ctx, m->vtype, &ms, &ma);
                            if (ci->is_union) {
                                if (ms > struct_size) struct_size = ms;
                            } else {
                                struct_size += ms; 
                            }
                            if (ma > struct_align) struct_align = ma;
                            m = m->next;
                        }
                        s = struct_size; a = struct_align;
                    }
                }
                break;
            }
            default: s = 4; a = 4; break;
        }
    }
    if (t.array_size > 0) s *= t.array_size;
    *out_size = s; *out_align = a;
}

void scan_classes(CodegenCtx *ctx, ASTNode *node, const char *prefix) {
    while (node) {
        if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            ClassInfo *ci = malloc(sizeof(ClassInfo));
            
            if (prefix && strlen(prefix) > 0) {
                char mangled[256];
                sprintf(mangled, "%s_%s", prefix, cn->name);
                ci->name = strdup(mangled);
                free(cn->name); cn->name = strdup(mangled);
            } else {
                ci->name = strdup(cn->name);
            }

            ci->parent_name = cn->parent_name ? strdup(cn->parent_name) : NULL;
            ci->struct_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), ci->name);
            ci->is_extern = cn->is_extern;
            ci->is_union = cn->is_union;
            ci->members = NULL;
            ci->method_names = NULL;
            ci->method_count = 0;
            ci->trait_count = cn->traits.count;
            ci->trait_names = NULL;
            ci->trait_offsets = NULL;
            if (ci->trait_count > 0) {
                ci->trait_names = malloc(sizeof(char*) * ci->trait_count);
                for(int i=0; i<ci->trait_count; i++) ci->trait_names[i] = strdup(cn->traits.names[i]);
            }
            add_class_info(ctx, ci);
        } else if (node->type == NODE_NAMESPACE) {
            NamespaceNode *ns = (NamespaceNode*)node;
            char new_prefix[256];
            if (prefix && strlen(prefix) > 0) sprintf(new_prefix, "%s_%s", prefix, ns->name);
            else strcpy(new_prefix, ns->name);
            add_namespace_name(ctx, new_prefix);
            scan_classes(ctx, ns->body, new_prefix);
        }
        node = node->next;
    }
}

void scan_enums(CodegenCtx *ctx, ASTNode *node, const char *prefix) {
    while (node) {
        if (node->type == NODE_ENUM) {
            EnumNode *en = (EnumNode*)node;
            EnumInfo *ei = malloc(sizeof(EnumInfo));
            if (prefix && strlen(prefix) > 0) {
                char mangled[256];
                sprintf(mangled, "%s_%s", prefix, en->name);
                ei->name = strdup(mangled);
            } else {
                ei->name = strdup(en->name);
            }
            ei->entries = NULL;
            EnumEntryInfo **tail = &ei->entries;
            EnumEntry *curr_ent = en->entries;
            while (curr_ent) {
                EnumEntryInfo *eei = malloc(sizeof(EnumEntryInfo));
                eei->name = strdup(curr_ent->name);
                eei->value = curr_ent->value;
                eei->next = NULL;
                *tail = eei;
                tail = &eei->next;
                LLVMValueRef const_val = LLVMConstInt(LLVMInt32Type(), curr_ent->value, 0);
                VarType vt = {TYPE_INT, 0, NULL, 0, 0};
                add_symbol(ctx, curr_ent->name, const_val, LLVMInt32Type(), vt, 0, 0);
                if (ctx->symbols) ctx->symbols->is_direct_value = 1;
                curr_ent = curr_ent->next;
            }
            ei->to_string_func = generate_enum_to_string_func(ctx, ei);
            add_enum_info(ctx, ei);
        } else if (node->type == NODE_NAMESPACE) {
            NamespaceNode *ns = (NamespaceNode*)node;
            char new_prefix[256];
            if (prefix && strlen(prefix) > 0) sprintf(new_prefix, "%s_%s", prefix, ns->name);
            else strcpy(new_prefix, ns->name);
            scan_enums(ctx, ns->body, new_prefix);
        }
        node = node->next;
    }
}

void register_trait_offset(ClassInfo *ci, const char *trait_name, int offset) {
    TraitOffset *to = malloc(sizeof(TraitOffset));
    to->trait_name = strdup(trait_name);
    to->offset_index = offset;
    to->next = ci->trait_offsets;
    ci->trait_offsets = to;
}

void scan_class_bodies(CodegenCtx *ctx, ASTNode *node) {
    while (node) {
        if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            if (cn->is_extern) { node = node->next; continue; }
            ClassInfo *ci = find_class(ctx, cn->name);
            if (ci) {
                int member_count = 0;
                if (ci->parent_name) {
                    ClassInfo *parent = find_class(ctx, ci->parent_name);
                    if (parent) { ClassMember *pm = parent->members; while(pm) { member_count++; pm = pm->next; } }
                }
                for (int i=0; i<ci->trait_count; i++) {
                    ClassInfo *trait = find_class(ctx, ci->trait_names[i]);
                    if (trait) { ClassMember *tm = trait->members; while(tm) { member_count++; tm = tm->next; } }
                }
                ASTNode *m = cn->members;
                while(m) { if (m->type == NODE_VAR_DECL) member_count++; m = m->next; }
                
                int idx = 0;
                ClassMember **tail = &ci->members;
                
                if (ci->parent_name) {
                    ClassInfo *parent = find_class(ctx, ci->parent_name);
                    if (parent) {
                        TraitOffset *pto = parent->trait_offsets;
                        while(pto) { register_trait_offset(ci, pto->trait_name, pto->offset_index); pto = pto->next; }
                        ClassMember *pm = parent->members;
                        while(pm) { tail = append_member(ctx, ci, tail, &idx, pm->name, pm->vtype, pm->type, NULL); pm = pm->next; }
                    }
                }
                for(int i=0; i<ci->trait_count; i++) {
                    ClassInfo *trait = find_class(ctx, ci->trait_names[i]);
                    if (trait) {
                        register_trait_offset(ci, ci->trait_names[i], idx);
                        ClassMember *tm = trait->members;
                        while(tm) { tail = append_member(ctx, ci, tail, &idx, tm->name, tm->vtype, tm->type, NULL); tm = tm->next; }
                    }
                }
                m = cn->members;
                while(m) {
                    if (m->type == NODE_VAR_DECL) {
                        VarDeclNode *vd = (VarDeclNode*)m;
                        LLVMTypeRef mt = LLVMInt32Type();
                        VarType mvt = vd->var_type;
                        if (vd->is_array) {
                             LLVMTypeRef et = get_llvm_type(ctx, vd->var_type);
                             unsigned int sz = 10;
                             if (vd->array_size && vd->array_size->type == NODE_LITERAL) sz = ((LiteralNode*)vd->array_size)->val.int_val;
                             mt = LLVMArrayType(et, sz);
                             mvt.array_size = sz;
                        } else {
                             mt = get_llvm_type(ctx, vd->var_type);
                        }
                        tail = append_member(ctx, ci, tail, &idx, vd->name, mvt, mt, vd->initializer);
                    }
                    m = m->next;
                }
                
                if (ci->is_union) {
                    size_t max_size = 0, max_align = 1;
                    LLVMTypeRef best_align_type = LLVMInt8Type();
                    ClassMember *cm = ci->members;
                    while(cm) {
                        size_t s, a;
                        get_type_size_align(ctx, cm->vtype, &s, &a);
                        if (s > max_size) max_size = s;
                        if (a > max_align) {
                            max_align = a;
                            if (a == 8) best_align_type = LLVMInt64Type();
                            else if (a == 4) best_align_type = LLVMInt32Type();
                            else if (a == 2) best_align_type = LLVMInt16Type();
                        }
                        cm = cm->next;
                    }
                    if (member_count > 0) {
                        size_t align_type_size = (max_align >= 8) ? 8 : max_align;
                        LLVMTypeRef *union_elems = NULL;
                        int union_elem_count = 1;
                        if (max_size > align_type_size) {
                            union_elem_count = 2;
                            union_elems = malloc(sizeof(LLVMTypeRef) * 2);
                            union_elems[0] = best_align_type;
                            union_elems[1] = LLVMArrayType(LLVMInt8Type(), max_size - align_type_size);
                        } else {
                            union_elems = malloc(sizeof(LLVMTypeRef) * 1);
                            union_elems[0] = best_align_type;
                        }
                        LLVMStructSetBody(ci->struct_type, union_elems, union_elem_count, false);
                        free(union_elems);
                    } else LLVMStructSetBody(ci->struct_type, NULL, 0, false);
                } else {
                    LLVMTypeRef *element_types = malloc(sizeof(LLVMTypeRef) * (member_count > 0 ? member_count : 1));
                    ClassMember *cm = ci->members;
                    int i = 0;
                    while(cm) { element_types[i++] = cm->type; cm = cm->next; }
                    LLVMStructSetBody(ci->struct_type, member_count > 0 ? element_types : NULL, member_count, false);
                    free(element_types);
                }
            }
        } else if (node->type == NODE_NAMESPACE) {
            scan_class_bodies(ctx, ((NamespaceNode*)node)->body);
        }
        node = node->next;
    }
}

void scan_functions(CodegenCtx *ctx, ASTNode *node, const char *prefix) {
    while(node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            char *sym_name = fd->mangled_name ? fd->mangled_name : fd->name;
            int pcount = 0;
            Parameter *p = fd->params;
            while(p) { pcount++; p=p->next; }
            VarType *ptypes = malloc(sizeof(VarType) * pcount);
            p = fd->params;
            int i = 0;
            while(p) { ptypes[i++] = p->type; p=p->next; }
            
            VarType rt = fd->ret_type;
            if (fd->is_flux) {
                rt = (VarType){TYPE_CHAR, 1, NULL, 0, 0}; // char* (handle)
            }

            add_func_symbol(ctx, sym_name, rt, ptypes, pcount, fd->is_flux);
            if (fd->is_flux) ctx->functions->yield_type = fd->ret_type;
        } else if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            ClassInfo *ci = find_class(ctx, cn->name);
            ASTNode *m = cn->members;
            while(m) {
                if (m->type == NODE_FUNC_DEF) {
                    FuncDefNode *fd = (FuncDefNode*)m;
                    char *mangled = fd->mangled_name ? strdup(fd->mangled_name) : NULL;
                    if (!mangled) { char buf[256]; sprintf(buf, "%s_%s", cn->name, fd->name); mangled = strdup(buf); }
                    int pcount = 0;
                    Parameter *p = fd->params;
                    while(p) { pcount++; p=p->next; }
                    VarType *ptypes = NULL;
                    if (pcount > 0) {
                        ptypes = malloc(sizeof(VarType) * pcount);
                        p = fd->params;
                        int i = 0;
                        while(p) { ptypes[i++] = p->type; p=p->next; }
                    }
                    VarType rt = fd->ret_type;
                    if (fd->is_flux) rt = (VarType){TYPE_CHAR, 1, NULL, 0, 0}; 

                    add_func_symbol(ctx, mangled, rt, ptypes, pcount, fd->is_flux);
                    if (fd->is_flux) ctx->functions->yield_type = fd->ret_type;
                    if (ci) {
                        ci->method_count++;
                        ci->method_names = realloc(ci->method_names, sizeof(char*) * ci->method_count);
                        ci->method_names[ci->method_count-1] = strdup(fd->name);
                    }
                    free(mangled);
                }
                m = m->next;
            }
        } else if (node->type == NODE_NAMESPACE) {
             NamespaceNode *ns = (NamespaceNode*)node;
             char new_prefix[256];
             if (prefix && strlen(prefix) > 0) sprintf(new_prefix, "%s_%s", prefix, ns->name);
             else strcpy(new_prefix, ns->name);
             scan_functions(ctx, ns->body, new_prefix);
        }
        node = node->next;
    }
}

LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name, const char *source) {
  LLVMModuleRef module = LLVMModuleCreateWithName(module_name);
  LLVMBuilderRef builder = LLVMCreateBuilder();

  CodegenCtx ctx;
  codegen_init_ctx(&ctx, module, builder, source);
  
  scan_classes(&ctx, root, NULL);
  scan_enums(&ctx, root, NULL);
  scan_class_bodies(&ctx, root);
  scan_functions(&ctx, root, NULL);

  // Code Gen
  ASTNode *curr = root;
  while (curr) {
    if (curr->type == NODE_FUNC_DEF) {
        FuncDefNode *fd = (FuncDefNode*)curr;
        if (fd->is_flux) {
            codegen_flux_def(&ctx, fd);
        } else {
            codegen_func_def(&ctx, fd);
        }
    }
    if (curr->type == NODE_CLASS) {
        ClassNode *cn = (ClassNode*)curr;
        ASTNode *m = cn->members;
        while(m) {
            if (m->type == NODE_FUNC_DEF) {
                FuncDefNode *fd = (FuncDefNode*)m;
                fd->class_name = cn->name;
                if (fd->is_flux) {
                    char flux_name[256];
                    snprintf(flux_name, 256, "%s_%s", cn->name, fd->name);
                    char *old_name = fd->name;
                    fd->name = flux_name; 
                    codegen_flux_def(&ctx, fd);
                    fd->name = old_name;
                } else {
                    codegen_func_def(&ctx, fd);
                }
            }
            m = m->next;
        }
    }
    if (curr->type == NODE_NAMESPACE) {
        codegen_node(&ctx, curr);
    }
    curr = curr->next;
  }

  LLVMDisposeBuilder(builder);
  return module;
}
