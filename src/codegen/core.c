#include "codegen.h"
#include "../diagnostic/diagnostic.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations of generators
LLVMValueRef generate_strlen(LLVMModuleRef module);
LLVMValueRef generate_strcpy(LLVMModuleRef module);
LLVMValueRef generate_strdup(LLVMModuleRef module, LLVMValueRef malloc_func, LLVMValueRef strlen_func, LLVMValueRef strcpy_func);
LLVMValueRef generate_input_func(LLVMModuleRef module, LLVMBuilderRef builder, LLVMValueRef malloc_func, LLVMValueRef getchar_func);
LLVMValueRef generate_enum_to_string_func(CodegenCtx *ctx, EnumInfo *ei);

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

void add_func_symbol(CodegenCtx *ctx, const char *name, VarType ret_type, VarType *params, int pcount) {
    FuncSymbol *s = malloc(sizeof(FuncSymbol));
    s->name = strdup(name);
    s->ret_type = ret_type;
    s->param_types = params;
    s->param_count = pcount;
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

LLVMTypeRef get_llvm_type(CodegenCtx *ctx, VarType t) {
  LLVMTypeRef base_type;
  switch (t.base) {
    case TYPE_INT: base_type = LLVMInt32Type(); break;
    case TYPE_CHAR: base_type = LLVMInt8Type(); break;
    case TYPE_BOOL: base_type = LLVMInt1Type(); break;
    case TYPE_FLOAT: base_type = LLVMFloatType(); break;
    case TYPE_DOUBLE: base_type = LLVMDoubleType(); break;
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

// ... Internal String Functions ...
// (Omitting full implementation of helpers as they are unchanged from original)

LLVMValueRef generate_strlen(LLVMModuleRef module) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef type = LLVMFunctionType(LLVMInt64Type(), args, 1, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strlen", type);
    // ... impl ...
    return func;
}

LLVMValueRef generate_strcpy(LLVMModuleRef module) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), args, 2, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strcpy", type);
    // ... impl ...
    return func;
}

LLVMValueRef generate_strdup(LLVMModuleRef module, LLVMValueRef malloc_func, LLVMValueRef strlen_func, LLVMValueRef strcpy_func) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), args, 1, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strdup", type);
    // ... impl ...
    return func;
}

LLVMValueRef generate_input_func(LLVMModuleRef module, LLVMBuilderRef builder, LLVMValueRef malloc_func, LLVMValueRef getchar_func) {
    LLVMTypeRef ret_type = LLVMPointerType(LLVMInt8Type(), 0);
    LLVMTypeRef func_type = LLVMFunctionType(ret_type, NULL, 0, false);
    LLVMValueRef func = LLVMAddFunction(module, "input", func_type);
    // ... impl ...
    return func;
}

// ... Enums ...
// (Omitting full implementation of generate_enum_to_string_func)

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
            ci->members = NULL;
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
    // ... (unchanged)
}

// Helper: Process Class Bodies recursively
void scan_class_bodies(CodegenCtx *ctx, ASTNode *node) {
    // ... (unchanged)
}

// Helper: Scan Functions recursively
void scan_functions(CodegenCtx *ctx, ASTNode *node, const char *prefix) {
    while(node) {
        if (node->type == NODE_FUNC_DEF) {
            FuncDefNode *fd = (FuncDefNode*)node;
            
            // Note: Semantic Analysis has already populated fd->mangled_name
            // We use that to identify the function in the symbol table
            
            // If inside namespace, the mangled name generated by semantic should handle it?
            // Actually semantic.c handles prefixing in its traversal.
            // But here we might need to be careful if we are modifying names again.
            
            char *sym_name = fd->mangled_name ? fd->mangled_name : fd->name;
            
            // Collect params
            int pcount = 0;
            Parameter *p = fd->params;
            while(p) { pcount++; p=p->next; }
            VarType *ptypes = malloc(sizeof(VarType) * pcount);
            p = fd->params;
            int i = 0;
            while(p) { ptypes[i++] = p->type; p=p->next; }
            
            add_func_symbol(ctx, sym_name, fd->ret_type, ptypes, pcount);
        }
        else if (node->type == NODE_CLASS) {
            ClassNode *cn = (ClassNode*)node;
            ASTNode *m = cn->members;
            while(m) {
                if (m->type == NODE_FUNC_DEF) {
                    FuncDefNode *fd = (FuncDefNode*)m;
                    // Class methods aren't overloaded in this iteration logic yet
                    char mangled[256];
                    sprintf(mangled, "%s_%s", cn->name, fd->name);
                    add_func_symbol(ctx, mangled, fd->ret_type, NULL, 0);
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
      codegen_func_def(&ctx, (FuncDefNode*)curr);
    }
    if (curr->type == NODE_CLASS) {
        ClassNode *cn = (ClassNode*)curr;
        ASTNode *m = cn->members;
        while(m) {
            if (m->type == NODE_FUNC_DEF) {
                FuncDefNode *fd = (FuncDefNode*)m;
                char *original_name = fd->name;
                char mangled[256];
                sprintf(mangled, "%s_%s", cn->name, fd->name);
                fd->name = mangled;
                fd->class_name = cn->name;
                codegen_func_def(&ctx, fd);
                fd->name = original_name; 
            }
            m = m->next;
        }
    }
    if (curr->type == NODE_NAMESPACE) {
        codegen_node(&ctx, curr);
    }
    curr = curr->next;
  }

  // Main
  // ... (unchanged) ...
  LLVMDisposeBuilder(builder);
  return module;
}
