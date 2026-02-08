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

int get_trait_offset(ClassInfo *ci, const char *trait_name) {
    // Check direct traits
    TraitOffset *to = ci->trait_offsets;
    while(to) {
        if (strcmp(to->trait_name, trait_name) == 0) return to->offset_index;
        to = to->next;
    }
    
    // Check parent
    if (ci->parent_name) {
        ClassInfo *parent = find_class(NULL, ci->parent_name); // Assuming globals can be found or we need ctx?
        // Limitation: find_class uses ctx. We must pass ctx to get_trait_offset if we want to search parent.
        // For now, scan_class_bodies ensures inheritance copies offsets, so checking direct 'trait_offsets' should suffice
        // IF we copied them correctly.
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

// --- Internal String Functions ---

LLVMValueRef generate_strlen(LLVMModuleRef module) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef func_type = LLVMFunctionType(LLVMInt64Type(), args, 1, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strlen", func_type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlock(func, "loop");
    LLVMBasicBlockRef end = LLVMAppendBasicBlock(func, "end");
    
    LLVMBuilderRef builder = LLVMCreateBuilder();
    
    // Entry
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMValueRef str = LLVMGetParam(func, 0);
    LLVMBuildBr(builder, loop);
    
    // Loop
    LLVMPositionBuilderAtEnd(builder, loop);
    LLVMValueRef ptr = LLVMBuildPhi(builder, LLVMPointerType(LLVMInt8Type(), 0), "ptr");
    LLVMValueRef incoming_vals_entry[] = { str };
    LLVMBasicBlockRef incoming_blocks_entry[] = { entry };
    LLVMAddIncoming(ptr, incoming_vals_entry, incoming_blocks_entry, 1);
    
    LLVMValueRef ch = LLVMBuildLoad2(builder, LLVMInt8Type(), ptr, "ch");
    LLVMValueRef is_null = LLVMBuildICmp(builder, LLVMIntEQ, ch, LLVMConstInt(LLVMInt8Type(), 0, 0), "is_null");
    
    LLVMValueRef next_ptr = LLVMBuildGEP2(builder, LLVMInt8Type(), ptr, (LLVMValueRef[]){ LLVMConstInt(LLVMInt64Type(), 1, 0) }, 1, "next_ptr");
    
    // Add phi incoming for loop back
    LLVMValueRef incoming_vals_loop[] = { next_ptr };
    LLVMBasicBlockRef incoming_blocks_loop[] = { loop };
    LLVMAddIncoming(ptr, incoming_vals_loop, incoming_blocks_loop, 1);
    
    LLVMBuildCondBr(builder, is_null, end, loop);
    
    // End
    LLVMPositionBuilderAtEnd(builder, end);
    LLVMValueRef start_int = LLVMBuildPtrToInt(builder, str, LLVMInt64Type(), "start_int");
    LLVMValueRef end_int = LLVMBuildPtrToInt(builder, ptr, LLVMInt64Type(), "end_int");
    LLVMValueRef len = LLVMBuildSub(builder, end_int, start_int, "len");
    LLVMBuildRet(builder, len);
    
    LLVMDisposeBuilder(builder);
    return func;
}

LLVMValueRef generate_strcpy(LLVMModuleRef module) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0), LLVMPointerType(LLVMInt8Type(), 0) }; // dest, src
    LLVMTypeRef func_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), args, 2, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strcpy", func_type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlock(func, "loop");
    LLVMBasicBlockRef end = LLVMAppendBasicBlock(func, "end");
    
    LLVMBuilderRef builder = LLVMCreateBuilder();
    
    // Entry
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMValueRef dest = LLVMGetParam(func, 0);
    LLVMValueRef src = LLVMGetParam(func, 1);
    LLVMBuildBr(builder, loop);
    
    // Loop
    LLVMPositionBuilderAtEnd(builder, loop);
    
    // Phi for Dest Ptr
    LLVMValueRef curr_dest = LLVMBuildPhi(builder, LLVMPointerType(LLVMInt8Type(), 0), "curr_dest");
    LLVMValueRef inc_dest_vals[] = { dest };
    LLVMBasicBlockRef inc_dest_blks[] = { entry };
    LLVMAddIncoming(curr_dest, inc_dest_vals, inc_dest_blks, 1);
    
    // Phi for Src Ptr
    LLVMValueRef curr_src = LLVMBuildPhi(builder, LLVMPointerType(LLVMInt8Type(), 0), "curr_src");
    LLVMValueRef inc_src_vals[] = { src };
    LLVMBasicBlockRef inc_src_blks[] = { entry };
    LLVMAddIncoming(curr_src, inc_src_vals, inc_src_blks, 1);
    
    // Copy
    LLVMValueRef ch = LLVMBuildLoad2(builder, LLVMInt8Type(), curr_src, "ch");
    LLVMBuildStore(builder, ch, curr_dest);
    
    LLVMValueRef is_null = LLVMBuildICmp(builder, LLVMIntEQ, ch, LLVMConstInt(LLVMInt8Type(), 0, 0), "is_null");
    
    // Next Ptrs
    LLVMValueRef next_dest = LLVMBuildGEP2(builder, LLVMInt8Type(), curr_dest, (LLVMValueRef[]){ LLVMConstInt(LLVMInt64Type(), 1, 0) }, 1, "next_dest");
    LLVMValueRef next_src = LLVMBuildGEP2(builder, LLVMInt8Type(), curr_src, (LLVMValueRef[]){ LLVMConstInt(LLVMInt64Type(), 1, 0) }, 1, "next_src");
    
    // Update Phis
    LLVMValueRef loop_dest_vals[] = { next_dest };
    LLVMBasicBlockRef loop_dest_blks[] = { loop };
    LLVMAddIncoming(curr_dest, loop_dest_vals, loop_dest_blks, 1);

    LLVMValueRef loop_src_vals[] = { next_src };
    LLVMBasicBlockRef loop_src_blks[] = { loop };
    LLVMAddIncoming(curr_src, loop_src_vals, loop_src_blks, 1);
    
    LLVMBuildCondBr(builder, is_null, end, loop);
    
    // End
    LLVMPositionBuilderAtEnd(builder, end);
    LLVMBuildRet(builder, dest);
    
    LLVMDisposeBuilder(builder);
    return func;
}

LLVMValueRef generate_strdup(LLVMModuleRef module, LLVMValueRef malloc_func, LLVMValueRef strlen_func, LLVMValueRef strcpy_func) {
    LLVMTypeRef args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef func_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), args, 1, false);
    LLVMValueRef func = LLVMAddFunction(module, "__builtin_strdup", func_type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);
    
    LLVMValueRef src = LLVMGetParam(func, 0);
    
    // 1. len = strlen(src)
    LLVMValueRef len = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_func), strlen_func, &src, 1, "len");
    
    // 2. size = len + 1
    LLVMValueRef size = LLVMBuildAdd(builder, len, LLVMConstInt(LLVMInt64Type(), 1, 0), "size");
    
    // 3. mem = malloc(size)
    LLVMValueRef mem = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_func), malloc_func, &size, 1, "mem");
    
    // 4. strcpy(mem, src)
    LLVMValueRef cp_args[] = { mem, src };
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(strcpy_func), strcpy_func, cp_args, 2, "");
    
    // 5. return mem
    LLVMBuildRet(builder, mem);
    
    LLVMDisposeBuilder(builder);
    return func;
}

LLVMValueRef generate_input_func(LLVMModuleRef module, LLVMBuilderRef builder, LLVMValueRef malloc_func, LLVMValueRef getchar_func) {
    // A simple input function that allocates a fixed buffer (e.g. 1024)
    // For a production compiler, dynamic resizing is needed.
    
    LLVMTypeRef ret_type = LLVMPointerType(LLVMInt8Type(), 0);
    LLVMTypeRef func_type = LLVMFunctionType(ret_type, NULL, 0, false);
    LLVMValueRef func = LLVMAddFunction(module, "input", func_type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlock(func, "loop");
    LLVMBasicBlockRef end = LLVMAppendBasicBlock(func, "end");
    
    LLVMBuilderRef func_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(func_builder, entry);
    
    // Allocate buffer: 1024 bytes
    LLVMValueRef size = LLVMConstInt(LLVMInt64Type(), 1024, 0);
    LLVMValueRef buf = LLVMBuildCall2(func_builder, LLVMGlobalGetValueType(malloc_func), malloc_func, &size, 1, "buffer");
    
    LLVMValueRef idx_ptr = LLVMBuildAlloca(func_builder, LLVMInt64Type(), "idx");
    LLVMBuildStore(func_builder, LLVMConstInt(LLVMInt64Type(), 0, 0), idx_ptr);
    
    LLVMBuildBr(func_builder, loop);
    
    // Loop
    LLVMPositionBuilderAtEnd(func_builder, loop);
    LLVMValueRef ch_int = LLVMBuildCall2(func_builder, LLVMGlobalGetValueType(getchar_func), getchar_func, NULL, 0, "ch_int");
    
    // Check EOF (-1) or \n (10)
    LLVMValueRef is_eof = LLVMBuildICmp(func_builder, LLVMIntEQ, ch_int, LLVMConstInt(LLVMInt32Type(), -1, 1), "is_eof");
    LLVMValueRef is_nl = LLVMBuildICmp(func_builder, LLVMIntEQ, ch_int, LLVMConstInt(LLVMInt32Type(), 10, 0), "is_nl");
    LLVMValueRef stop = LLVMBuildOr(func_builder, is_eof, is_nl, "stop");
    
    LLVMBasicBlockRef store_block = LLVMAppendBasicBlock(func, "store");
    LLVMBuildCondBr(func_builder, stop, end, store_block);
    
    // Store char
    LLVMPositionBuilderAtEnd(func_builder, store_block);
    LLVMValueRef ch_byte = LLVMBuildTrunc(func_builder, ch_int, LLVMInt8Type(), "ch_byte");
    LLVMValueRef curr_idx = LLVMBuildLoad2(func_builder, LLVMInt64Type(), idx_ptr, "curr_idx");
    
    // Safety check: if idx >= 1023, stop (leave room for null)
    LLVMValueRef limit_chk = LLVMBuildICmp(func_builder, LLVMIntULT, curr_idx, LLVMConstInt(LLVMInt64Type(), 1023, 0), "limit_chk");
    
    LLVMBasicBlockRef do_store = LLVMAppendBasicBlock(func, "do_store");
    LLVMBuildCondBr(func_builder, limit_chk, do_store, end);
    
    LLVMPositionBuilderAtEnd(func_builder, do_store);
    LLVMValueRef slot = LLVMBuildGEP2(func_builder, LLVMInt8Type(), buf, &curr_idx, 1, "slot");
    LLVMBuildStore(func_builder, ch_byte, slot);
    
    LLVMValueRef next_idx = LLVMBuildAdd(func_builder, curr_idx, LLVMConstInt(LLVMInt64Type(), 1, 0), "next_idx");
    LLVMBuildStore(func_builder, next_idx, idx_ptr);
    
    LLVMBuildBr(func_builder, loop);
    
    // End
    LLVMPositionBuilderAtEnd(func_builder, end);
    LLVMValueRef final_idx = LLVMBuildLoad2(func_builder, LLVMInt64Type(), idx_ptr, "final_idx");
    LLVMValueRef final_slot = LLVMBuildGEP2(func_builder, LLVMInt8Type(), buf, &final_idx, 1, "final_slot");
    LLVMBuildStore(func_builder, LLVMConstInt(LLVMInt8Type(), 0, 0), final_slot); // Null terminate
    
    LLVMBuildRet(func_builder, buf);
    
    LLVMDisposeBuilder(func_builder);
    return func;
}

LLVMValueRef generate_enum_to_string_func(CodegenCtx *ctx, EnumInfo *ei) {
    char func_name[512];
    sprintf(func_name, "%s_ToString", ei->name);
    
    LLVMTypeRef param_types[] = { LLVMInt32Type() };
    LLVMTypeRef func_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0), param_types, 1, false);
    LLVMValueRef func = LLVMAddFunction(ctx->module, func_name, func_type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef default_bb = LLVMAppendBasicBlock(func, "default");
    
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);
    
    LLVMValueRef val = LLVMGetParam(func, 0);
    
    // Count cases
    int count = 0;
    EnumEntryInfo *curr = ei->entries;
    while(curr) { count++; curr = curr->next; }
    
    LLVMValueRef switch_inst = LLVMBuildSwitch(builder, val, default_bb, count);
    
    curr = ei->entries;
    while(curr) {
        LLVMBasicBlockRef case_bb = LLVMAppendBasicBlock(func, "case");
        LLVMAddCase(switch_inst, LLVMConstInt(LLVMInt32Type(), curr->value, 0), case_bb);
        
        LLVMPositionBuilderAtEnd(builder, case_bb);
        LLVMValueRef str_val = LLVMBuildGlobalStringPtr(builder, curr->name, "enum_str");
        LLVMBuildRet(builder, str_val);
        
        curr = curr->next;
    }
    
    LLVMPositionBuilderAtEnd(builder, default_bb);
    LLVMValueRef unknown_str = LLVMBuildGlobalStringPtr(builder, "Unknown", "unknown_enum");
    LLVMBuildRet(builder, unknown_str);
    
    LLVMDisposeBuilder(builder);
    return func;
}

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
            ClassInfo *ci = find_class(ctx, cn->name);
            if (ci) {
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
                
                LLVMTypeRef *element_types = malloc(sizeof(LLVMTypeRef) * (member_count > 0 ? member_count : 1));
                int idx = 0;
                ClassMember **tail = &ci->members;
                
                // 1. Inherit Parent
                if (ci->parent_name) {
                    ClassInfo *parent = find_class(ctx, ci->parent_name);
                    if (parent) {
                        // Inherit offsets from parent
                        TraitOffset *pto = parent->trait_offsets;
                        while(pto) {
                            register_trait_offset(ci, pto->trait_name, pto->offset_index);
                            pto = pto->next;
                        }

                        ClassMember *pm = parent->members;
                        while(pm) {
                            element_types[idx] = pm->type;
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
                            element_types[idx] = tm->type;
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
                        
                        if (vd->is_array) {
                             LLVMTypeRef et = get_llvm_type(ctx, vd->var_type);
                             unsigned int sz = 10;
                             if (vd->array_size && vd->array_size->type == NODE_LITERAL) sz = ((LiteralNode*)vd->array_size)->val.int_val;
                             mt = LLVMArrayType(et, sz);
                             mvt.array_size = sz;
                        } else {
                             mt = get_llvm_type(ctx, vd->var_type);
                        }
                        
                        element_types[idx] = mt;
                        tail = append_member(ctx, ci, tail, &idx, vd->name, mvt, mt, vd->initializer);
                    }
                    m = m->next;
                }
                
                if (member_count > 0)
                    LLVMStructSetBody(ci->struct_type, element_types, member_count, false);
                else
                    LLVMStructSetBody(ci->struct_type, NULL, 0, false); // Empty struct

                free(element_types);
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

  // Generate Main
  FuncSymbol *main_sym = find_func_symbol(&ctx, "main");
  if (main_sym) {
      // Logic for main entry point already generated by codegen_func_def if it exists
  }

  LLVMDisposeBuilder(builder);
  return module;
}
