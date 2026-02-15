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

    // Flux init
    ctx->flux_promise_val = NULL;
    ctx->flux_promise_type = NULL;
    ctx->flux_coro_hdl = NULL; // Init handle
    ctx->flux_return_block = NULL;

    // TODO add open, fopen, .etc

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
    
    // setjmp - returns i32, takes i8* (pointer to jmp_buf)
    LLVMTypeRef setjmp_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef setjmp_type = LLVMFunctionType(LLVMInt32Type(), setjmp_args, 1, false);
    ctx->setjmp_func = LLVMAddFunction(module, "setjmp", setjmp_type);
    
    // longjmp - returns void, takes i8* (pointer to jmp_buf) and i32
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

    // LLVM Coroutine Intrinsics
    // llvm.coro.id
    LLVMTypeRef id_args[] = { LLVMInt32Type(), LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->coro_id = LLVMAddFunction(module, "llvm.coro.id", LLVMFunctionType(LLVMTokenTypeInContext(LLVMGetGlobalContext()), id_args, 4, false));

    // llvm.coro.size
    ctx->coro_size = LLVMAddFunction(module, "llvm.coro.size.i64", LLVMFunctionType(LLVMInt64Type(), NULL, 0, false));

    // llvm.coro.begin
    LLVMTypeRef begin_args[] = { LLVMTokenTypeInContext(LLVMGetGlobalContext()), LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->coro_begin = LLVMAddFunction(module, "llvm.coro.begin", LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), begin_args, 2, false));

    // llvm.coro.save (Added)
    LLVMTypeRef save_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->coro_save = LLVMAddFunction(module, "llvm.coro.save", LLVMFunctionType(LLVMTokenTypeInContext(LLVMGetGlobalContext()), save_args, 1, false));

    // llvm.coro.suspend
    LLVMTypeRef suspend_args[] = { LLVMTokenTypeInContext(LLVMGetGlobalContext()), LLVMInt1Type() };
    ctx->coro_suspend = LLVMAddFunction(module, "llvm.coro.suspend", LLVMFunctionType(LLVMInt8Type(), suspend_args, 2, false));

    // llvm.coro.end
    LLVMTypeRef end_args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMInt1Type() };
    ctx->coro_end = LLVMAddFunction(module, "llvm.coro.end", LLVMFunctionType(LLVMInt1Type(), end_args, 2, false));

    // llvm.coro.free
    LLVMTypeRef cfree_args[] = { LLVMTokenTypeInContext(LLVMGetGlobalContext()), LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->coro_free = LLVMAddFunction(module, "llvm.coro.free", LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), cfree_args, 2, false));

    // llvm.coro.resume
    LLVMTypeRef resume_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    ctx->coro_resume = LLVMAddFunction(module, "llvm.coro.resume", LLVMFunctionType(LLVMVoidType(), resume_args, 1, false));

    // llvm.coro.destroy
    ctx->coro_destroy = LLVMAddFunction(module, "llvm.coro.destroy", LLVMFunctionType(LLVMVoidType(), resume_args, 1, false));

    // llvm.coro.promise
    LLVMTypeRef prom_args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMInt32Type(), LLVMInt1Type() };
    ctx->coro_promise = LLVMAddFunction(module, "llvm.coro.promise", LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), prom_args, 3, false));
    
    // llvm.coro.done
    ctx->coro_done = LLVMAddFunction(module, "llvm.coro.done", LLVMFunctionType(LLVMInt1Type(), resume_args, 1, false));

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
    // TODO: if in repl, do not exit
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
    s->yield_type = (VarType){0}; // Init to zero/unknown
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
    if (!ctx) return NULL; // Safety against null context
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

// FIX: Added ctx to safely recursively lookup parents
int get_trait_offset(CodegenCtx *ctx, ClassInfo *ci, const char *trait_name) {
    // Check direct traits
    TraitOffset *to = ci->trait_offsets;
    while(to) {
        if (strcmp(to->trait_name, trait_name) == 0) return to->offset_index;
        to = to->next;
    }
    
    // Check parent
    if (ci->parent_name) {
        ClassInfo *parent = find_class(ctx, ci->parent_name); 
        if (parent) return get_trait_offset(ctx, parent, trait_name);
    }
    return -1;
}

LLVMTypeRef get_llvm_type(CodegenCtx *ctx, VarType t) {
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

// --- Helpers ---

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

// Helper to estimate size and alignment for union layout
void get_type_size_align(CodegenCtx *ctx, VarType t, size_t *out_size, size_t *out_align) {
    size_t s = 0;
    size_t a = 1;
    
    if (t.ptr_depth > 0) {
        s = 8; a = 8;
    } else {
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
                        // Estimate struct size roughly by summing members
                        // Note: This ignores padding, so it is a lower bound estimate.
                        // For Unions, this recursive call is critical.
                        size_t struct_size = 0;
                        size_t struct_align = 1;
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
                        s = struct_size;
                        a = struct_align;
                    }
                }
                break;
            }
            default: s = 4; a = 4; break;
        }
    }
    
    if (t.array_size > 0) {
        s *= t.array_size;
        // Alignment stays same as element alignment
    }
    
    *out_size = s;
    *out_align = a;
}

// --- Internal String Functions ---

// Helper: Scan for Classes recursively
void scan_classes(CodegenCtx *ctx, ASTNode *node, const char *prefix) {
    while (node) {
        if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            ClassInfo *ci = malloc(sizeof(ClassInfo));
            
            if (prefix && strlen(prefix) > 0) {
                char mangled[256];
                sprintf(mangled, "%s_%s", prefix, cn->name);
                ci->name = strdup(mangled);
                free(cn->name);
                cn->name = strdup(mangled);
            } else {
                ci->name = strdup(cn->name);
            }

            ci->parent_name = cn->parent_name ? strdup(cn->parent_name) : NULL;
            ci->struct_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), ci->name);
            ci->is_extern = cn->is_extern;
            ci->is_union = cn->is_union; // COPY UNION FLAG
            ci->members = NULL;
            ci->method_names = NULL;
            ci->method_count = 0;
            
            // Capture Traits
            ci->trait_count = cn->traits.count;
            ci->trait_names = NULL;
            ci->trait_offsets = NULL;
            if (ci->trait_count > 0) {
                ci->trait_names = malloc(sizeof(char*) * ci->trait_count);
                for(int i=0; i<ci->trait_count; i++) {
                    ci->trait_names[i] = strdup(cn->traits.names[i]);
                }
            }
            
            add_class_info(ctx, ci);
        }
        else if (node->type == NODE_NAMESPACE) {
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

// Helper: Scan for Enums recursively
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
                
                // REGISTER MEMBER AS CONSTANT SYMBOL
                // This allows 'let x = EnumMember;' to work
                LLVMValueRef const_val = LLVMConstInt(LLVMInt32Type(), curr_ent->value, 0);
                VarType vt = {TYPE_INT, 0, NULL, 0, 0};
                add_symbol(ctx, curr_ent->name, const_val, LLVMInt32Type(), vt, 0, 0);
                // Mark as direct value so we don't try to load it from memory
                if (ctx->symbols) ctx->symbols->is_direct_value = 1;

                curr_ent = curr_ent->next;
            }
            
            // Generate to_string function
            ei->to_string_func = generate_enum_to_string_func(ctx, ei);
            
            add_enum_info(ctx, ei);
        }
        else if (node->type == NODE_NAMESPACE) {
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

// Helper: Process Class Bodies recursively
void scan_class_bodies(CodegenCtx *ctx, ASTNode *node) {
    while (node) {
        if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            if (cn->is_extern) {
                node = node->next;
                continue; // Do not emit a body for opaque extern structs
            }
            ClassInfo *ci = find_class(ctx, cn->name);
            if (ci) {
                // Collect Members First
                int member_count = 0;
                
                // Inherited members
                if (ci->parent_name) {
                    ClassInfo *parent = find_class(ctx, ci->parent_name);
                    if (parent) {
                        ClassMember *pm = parent->members;
                        while(pm) { member_count++; pm = pm->next; }
                    }
                }
                
                // Trait members
                for (int i=0; i<ci->trait_count; i++) {
                    ClassInfo *trait = find_class(ctx, ci->trait_names[i]);
                    if (trait) {
                        ClassMember *tm = trait->members;
                        while(tm) { member_count++; tm = tm->next; }
                    }
                }
                
                // Own members
                ASTNode *m = cn->members;
                while(m) {
                    if (m->type == NODE_VAR_DECL) member_count++;
                    m = m->next;
                }
                
                // Temporarily store member definitions to calculate union sizes if needed
                int idx = 0;
                ClassMember **tail = &ci->members;
                
                // We will populate ci->members list fully first, but NOT set the LLVM struct body yet if it's a union.
                
                // 1. Inherit Parent
                if (ci->parent_name) {
                    ClassInfo *parent = find_class(ctx, ci->parent_name);
                    if (parent) {
                        TraitOffset *pto = parent->trait_offsets;
                        while(pto) {
                            register_trait_offset(ci, pto->trait_name, pto->offset_index);
                            pto = pto->next;
                        }
                        ClassMember *pm = parent->members;
                        while(pm) {
                            tail = append_member(ctx, ci, tail, &idx, pm->name, pm->vtype, pm->type, NULL);
                            pm = pm->next;
                        }
                    }
                }
                
                // 2. Mixin Traits
                for(int i=0; i<ci->trait_count; i++) {
                    ClassInfo *trait = find_class(ctx, ci->trait_names[i]);
                    if (trait) {
                        register_trait_offset(ci, ci->trait_names[i], idx);
                        ClassMember *tm = trait->members;
                        while(tm) {
                            tail = append_member(ctx, ci, tail, &idx, tm->name, tm->vtype, tm->type, NULL);
                            tm = tm->next;
                        }
                    }
                }
                
                // 3. Own Members
                m = cn->members;
                while(m) {
                    if (m->type == NODE_VAR_DECL) {
                        VarDeclNode *vd = (VarDeclNode*)m;
                        LLVMTypeRef mt = LLVMInt32Type();
                        VarType mvt = vd->var_type;
                        
                        if (vd->var_type.base == TYPE_CLASS && vd->var_type.ptr_depth == 0) {
                            ClassInfo *mem_ci = find_class(ctx, vd->var_type.class_name);
                            if (mem_ci && mem_ci->is_extern) {
                                codegen_error(ctx, m, "Cannot embed extern opaque type by value");
                            }
                        }

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
                
                // --- STRUCTURE BODY GENERATION ---
                
                if (ci->is_union) {
                    // UNION LAYOUT: { LargestAlignedType, [Padding x i8] }
                    // This ensures the struct is big enough and aligned enough.
                    // All members effectively start at offset 0 (via bitcasts in expr.c).
                    
                    size_t max_size = 0;
                    size_t max_align = 1;
                    LLVMTypeRef best_align_type = LLVMInt8Type(); // Default fallback
                    
                    ClassMember *cm = ci->members;
                    while(cm) {
                        size_t s, a;
                        get_type_size_align(ctx, cm->vtype, &s, &a);
                        if (s > max_size) max_size = s;
                        if (a > max_align) {
                            max_align = a;
                            // Pick a representative type for this alignment
                            if (a == 8) best_align_type = LLVMInt64Type();
                            else if (a == 4) best_align_type = LLVMInt32Type();
                            else if (a == 2) best_align_type = LLVMInt16Type();
                            else best_align_type = LLVMInt8Type();
                        }
                        cm = cm->next;
                    }
                    
                    if (member_count > 0) {
                        // Calculate padding needed
                        // Size of best_align_type might be smaller than max_size
                        // e.g. best_align=8 (i64), max_size=10 (char[10]).
                        // Struct = { i64, [2 x i8] }
                        
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
                    } else {
                        LLVMStructSetBody(ci->struct_type, NULL, 0, false);
                    }
                    
                } else {
                    // STANDARD STRUCT LAYOUT
                    LLVMTypeRef *element_types = malloc(sizeof(LLVMTypeRef) * (member_count > 0 ? member_count : 1));
                    ClassMember *cm = ci->members;
                    int i = 0;
                    while(cm) {
                        element_types[i++] = cm->type;
                        cm = cm->next;
                    }
                    
                    if (member_count > 0)
                        LLVMStructSetBody(ci->struct_type, element_types, member_count, false);
                    else
                        LLVMStructSetBody(ci->struct_type, NULL, 0, false); 

                    free(element_types);
                }
            }
        }
        else if (node->type == NODE_NAMESPACE) {
            scan_class_bodies(ctx, ((NamespaceNode*)node)->body);
        }
        node = node->next;
    }
}

// Helper: Scan Functions recursively
void scan_functions(CodegenCtx *ctx, ASTNode *node, const char *prefix) {
    while(node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            
            char *sym_name = fd->mangled_name ? fd->mangled_name : fd->name;
            
            // Collect params
            int pcount = 0;
            Parameter *p = fd->params;
            while(p) { pcount++; p=p->next; }
            VarType *ptypes = malloc(sizeof(VarType) * pcount);
            p = fd->params;
            int i = 0;
            while(p) { ptypes[i++] = p->type; p=p->next; }
            
            // If flux, we change ret_type to i8* (coroutine handle)
            VarType rt = fd->ret_type;
            if (fd->is_flux) {
                rt = (VarType){TYPE_CHAR, 1, NULL, 0, 0}; // char* (handle)
            }

            add_func_symbol(ctx, sym_name, rt, ptypes, pcount, fd->is_flux);
            
            // FIX: Store the original yield type in the symbol table for Flux functions
            if (fd->is_flux) {
                ctx->functions->yield_type = fd->ret_type;
            }
        }
        else if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            ClassInfo *ci = find_class(ctx, cn->name);

            ASTNode *m = cn->members;
            while(m) {
                if (m->type == NODE_FUNC_DEF) {
                    FuncDefNode *fd = (FuncDefNode*)m;
                    // FIX: Use the mangled name generated by semantic analysis
                    char *mangled;
                    if (fd->mangled_name) {
                        mangled = strdup(fd->mangled_name);
                    } else {
                        // Fallback (should not happen if semantic runs)
                        char buf[256];
                        sprintf(buf, "%s_%s", cn->name, fd->name);
                        mangled = strdup(buf);
                    }
                    
                    // FIX: Capture parameters correctly for method overload resolution
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
                    if (fd->is_flux) {
                        rt = (VarType){TYPE_CHAR, 1, NULL, 0, 0}; // char*
                    }

                    add_func_symbol(ctx, mangled, rt, ptypes, pcount, fd->is_flux);

                    // FIX: Store yield type for flux methods
                    if (fd->is_flux) {
                        ctx->functions->yield_type = fd->ret_type;
                    }
                    
                    // Add method metadata to ClassInfo
                    if (ci) {
                        ci->method_count++;
                        ci->method_names = realloc(ci->method_names, sizeof(char*) * ci->method_count);
                        ci->method_names[ci->method_count-1] = strdup(fd->name);
                    }

                    // Note: We don't free mangled here as add_func_symbol duplicates it? 
                    // No, add_func_symbol strdups it. We must free if we allocated strdup.
                    free(mangled);
                }
                m = m->next;
            }
        }
        else if (node->type == NODE_NAMESPACE) {
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
                    // Prepend Class Name for unique struct naming in flux
                    char flux_name[256];
                    snprintf(flux_name, 256, "%s_%s", cn->name, fd->name);
                    // Temporarily swap name for generation logic
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

  // Generate Main
  FuncSymbol *main_sym = find_func_symbol(&ctx, "main");
  if (main_sym) {
    // Logic for main entry point already generated by codegen_func_def if it exists
  }

  LLVMDisposeBuilder(builder);
  return module;
}
