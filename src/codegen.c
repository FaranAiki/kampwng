#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Symbol Table Entry
typedef struct Symbol {
    char *name;
    LLVMValueRef value;
    LLVMTypeRef type;
    struct Symbol *next;
} Symbol;

// Context
typedef struct {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMValueRef printf_func;
    LLVMTypeRef printf_type;
    Symbol *symbols; 
} CodegenCtx;

void add_symbol(CodegenCtx *ctx, const char *name, LLVMValueRef val, LLVMTypeRef type) {
    Symbol *s = malloc(sizeof(Symbol));
    s->name = strdup(name);
    s->value = val;
    s->type = type;
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

char* format_string(const char* input) {
    if (!input) return NULL;
    size_t len = strlen(input);
    char *new_str = malloc(len + 2);
    strcpy(new_str, input);
    if (len == 0 || input[len-1] != '\n') {
        new_str[len] = '\n';
        new_str[len+1] = '\0';
    }
    return new_str;
}

LLVMTypeRef get_llvm_type(VarType t) {
    switch (t) {
        case VAR_INT: return LLVMInt32Type();
        case VAR_CHAR: return LLVMInt8Type();
        case VAR_BOOL: return LLVMInt1Type();
        case VAR_FLOAT: return LLVMFloatType();
        case VAR_DOUBLE: return LLVMDoubleType();
        default: return LLVMInt32Type();
    }
}

// Forward Decl
LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node);
void codegen_node(CodegenCtx *ctx, ASTNode *node);

LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node) {
    if (!node) return LLVMConstInt(LLVMInt32Type(), 0, 0);

    if (node->type == NODE_LITERAL) {
        LiteralNode *l = (LiteralNode*)node;
        if (l->var_type == VAR_DOUBLE) return LLVMConstReal(LLVMDoubleType(), l->val.double_val);
        if (l->var_type == VAR_BOOL) return LLVMConstInt(LLVMInt1Type(), l->val.int_val, 0);
        return LLVMConstInt(get_llvm_type(l->var_type), l->val.int_val, 0);
    }
    else if (node->type == NODE_VAR_REF) {
        VarRefNode *r = (VarRefNode*)node;
        Symbol *sym = find_symbol(ctx, r->name);
        if (!sym) { fprintf(stderr, "Error: Undefined variable %s\n", r->name); exit(1); }
        return LLVMBuildLoad2(ctx->builder, sym->type, sym->value, r->name);
    }
    else if (node->type == NODE_BINARY_OP) {
        BinaryOpNode *op = (BinaryOpNode*)node;
        LLVMValueRef l = codegen_expr(ctx, op->left);
        LLVMValueRef r = codegen_expr(ctx, op->right);
        
        LLVMTypeRef l_type = LLVMTypeOf(l);
        LLVMTypeRef r_type = LLVMTypeOf(r);
        int is_float = (LLVMGetTypeKind(l_type) == LLVMDoubleTypeKind || LLVMGetTypeKind(r_type) == LLVMDoubleTypeKind ||
                        LLVMGetTypeKind(l_type) == LLVMFloatTypeKind || LLVMGetTypeKind(r_type) == LLVMFloatTypeKind);
        
        // Promotion to Double
        if (is_float) {
            if (LLVMGetTypeKind(l_type) != LLVMDoubleTypeKind) l = LLVMBuildUIToFP(ctx->builder, l, LLVMDoubleType(), "cast_l");
            if (LLVMGetTypeKind(r_type) != LLVMDoubleTypeKind) r = LLVMBuildUIToFP(ctx->builder, r, LLVMDoubleType(), "cast_r");
            
            switch (op->op) {
                case TOKEN_PLUS: return LLVMBuildFAdd(ctx->builder, l, r, "fadd");
                case TOKEN_MINUS: return LLVMBuildFSub(ctx->builder, l, r, "fsub");
                case TOKEN_STAR: return LLVMBuildFMul(ctx->builder, l, r, "fmul");
                case TOKEN_SLASH: return LLVMBuildFDiv(ctx->builder, l, r, "fdiv");
                // Comparisons
                case TOKEN_EQ: return LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, l, r, "feq");
                case TOKEN_NEQ: return LLVMBuildFCmp(ctx->builder, LLVMRealONE, l, r, "fneq");
                case TOKEN_LT: return LLVMBuildFCmp(ctx->builder, LLVMRealOLT, l, r, "flt");
                case TOKEN_GT: return LLVMBuildFCmp(ctx->builder, LLVMRealOGT, l, r, "fgt");
                case TOKEN_LTE: return LLVMBuildFCmp(ctx->builder, LLVMRealOLE, l, r, "fle");
                case TOKEN_GTE: return LLVMBuildFCmp(ctx->builder, LLVMRealOGE, l, r, "fge");
                default: return LLVMConstReal(LLVMDoubleType(), 0.0);
            }
        } else {
            // Integer
            // Ensure types match for strictness (implicitly handled by C-api usually but good practice)
            if (LLVMGetTypeKind(l_type) != LLVMGetTypeKind(r_type)) {
                // Cast to widest (assuming i32 here for simplicity)
                l = LLVMBuildIntCast(ctx->builder, l, LLVMInt32Type(), "cast_l");
                r = LLVMBuildIntCast(ctx->builder, r, LLVMInt32Type(), "cast_r");
            }

            switch (op->op) {
                case TOKEN_PLUS: return LLVMBuildAdd(ctx->builder, l, r, "add");
                case TOKEN_MINUS: return LLVMBuildSub(ctx->builder, l, r, "sub");
                case TOKEN_STAR: return LLVMBuildMul(ctx->builder, l, r, "mul");
                case TOKEN_SLASH: return LLVMBuildSDiv(ctx->builder, l, r, "div");
                case TOKEN_XOR: return LLVMBuildXor(ctx->builder, l, r, "xor");
                case TOKEN_LSHIFT: return LLVMBuildShl(ctx->builder, l, r, "shl");
                case TOKEN_RSHIFT: return LLVMBuildAShr(ctx->builder, l, r, "shr");
                // Comparisons (Signed)
                case TOKEN_EQ: return LLVMBuildICmp(ctx->builder, LLVMIntEQ, l, r, "eq");
                case TOKEN_NEQ: return LLVMBuildICmp(ctx->builder, LLVMIntNE, l, r, "neq");
                case TOKEN_LT: return LLVMBuildICmp(ctx->builder, LLVMIntSLT, l, r, "lt");
                case TOKEN_GT: return LLVMBuildICmp(ctx->builder, LLVMIntSGT, l, r, "gt");
                case TOKEN_LTE: return LLVMBuildICmp(ctx->builder, LLVMIntSLE, l, r, "le");
                case TOKEN_GTE: return LLVMBuildICmp(ctx->builder, LLVMIntSGE, l, r, "ge");
                default: return LLVMConstInt(LLVMInt32Type(), 0, 0);
            }
        }
    }
    return LLVMConstInt(LLVMInt32Type(), 0, 0);
}

void codegen_var_decl(CodegenCtx *ctx, VarDeclNode *node) {
    LLVMValueRef init_val = codegen_expr(ctx, node->initializer);
    LLVMTypeRef type = get_llvm_type(node->var_type);
    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, type, node->name);
    
    // Simple casting
    if (node->var_type == VAR_DOUBLE && LLVMGetTypeKind(LLVMTypeOf(init_val)) == LLVMIntegerTypeKind) {
        init_val = LLVMBuildSIToFP(ctx->builder, init_val, LLVMDoubleType(), "cast");
    } else if (node->var_type == VAR_BOOL && LLVMGetTypeKind(LLVMTypeOf(init_val)) == LLVMIntegerTypeKind) {
        // Cast int to bool (i32 -> i1)
        init_val = LLVMBuildTrunc(ctx->builder, init_val, LLVMInt1Type(), "to_bool");
    }

    LLVMBuildStore(ctx->builder, init_val, alloca);
    add_symbol(ctx, node->name, alloca, type);
}

void codegen_print(CodegenCtx *ctx, PrintNode *node) {
    if (!node->message) return;
    char *fmt = format_string(node->message);
    LLVMValueRef global_str = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "str");
    free(fmt);
    LLVMValueRef args[] = { global_str };
    LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, args, 1, "");
}

void codegen_loop(CodegenCtx *ctx, LoopNode *node) {
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "loop_cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "loop_body");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlock(func, "loop_end");

    LLVMValueRef counter_ptr = LLVMBuildAlloca(ctx->builder, LLVMInt64Type(), "loop_i");
    LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt64Type(), 0, 0), counter_ptr);
    LLVMBuildBr(ctx->builder, cond_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    LLVMValueRef cur_i = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), counter_ptr, "i_val");
    LLVMValueRef limit = codegen_expr(ctx, node->iterations);
    
    // Ensure limit is i64
    if (LLVMGetTypeKind(LLVMTypeOf(limit)) != LLVMIntegerTypeKind) {
         limit = LLVMBuildFPToUI(ctx->builder, limit, LLVMInt64Type(), "limit_cast");
    } else {
         limit = LLVMBuildIntCast(ctx->builder, limit, LLVMInt64Type(), "limit_cast");
    }

    LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntULT, cur_i, limit, "cmp");
    LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    codegen_node(ctx, node->body);
    
    LLVMValueRef cur_i_body = LLVMBuildLoad2(ctx->builder, LLVMInt64Type(), counter_ptr, "i_val_body");
    LLVMValueRef next_i = LLVMBuildAdd(ctx->builder, cur_i_body, LLVMConstInt(LLVMInt64Type(), 1, 0), "next_i");
    LLVMBuildStore(ctx->builder, next_i, counter_ptr);
    LLVMBuildBr(ctx->builder, cond_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
}

void codegen_if(CodegenCtx *ctx, IfNode *node) {
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    
    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlock(func, "if_then");
    LLVMBasicBlockRef else_bb = LLVMAppendBasicBlock(func, "if_else");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlock(func, "if_merge");

    // Condition
    LLVMValueRef cond = codegen_expr(ctx, node->condition);
    // Ensure bool (i1)
    if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
        cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond, LLVMConstInt(LLVMTypeOf(cond), 0, 0), "to_bool");
    }
    
    // Branch based on condition
    LLVMBuildCondBr(ctx->builder, cond, then_bb, else_bb);

    // Then Block
    LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
    codegen_node(ctx, node->then_body);
    // Only add branch to merge if the block doesn't already have a terminator (like return)
    if (!LLVMGetBasicBlockTerminator(then_bb)) {
        LLVMBuildBr(ctx->builder, merge_bb);
    }

    // Else Block
    LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
    if (node->else_body) {
        codegen_node(ctx, node->else_body);
    }
    if (!LLVMGetBasicBlockTerminator(else_bb)) {
        LLVMBuildBr(ctx->builder, merge_bb);
    }

    // Continue at Merge
    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
}

void codegen_node(CodegenCtx *ctx, ASTNode *node) {
    while (node) {
        if (node->type == NODE_PRINT) codegen_print(ctx, (PrintNode*)node);
        else if (node->type == NODE_LOOP) codegen_loop(ctx, (LoopNode*)node);
        else if (node->type == NODE_IF) codegen_if(ctx, (IfNode*)node);
        else if (node->type == NODE_VAR_DECL) codegen_var_decl(ctx, (VarDeclNode*)node);
        node = node->next;
    }
}

LLVMModuleRef codegen_generate(ASTNode *root, const char *module_name) {
    LLVMModuleRef module = LLVMModuleCreateWithName(module_name);
    LLVMBuilderRef builder = LLVMCreateBuilder();

    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef printf_type = LLVMFunctionType(LLVMInt32Type(), printf_args, 1, true);
    LLVMValueRef printf_func = LLVMAddFunction(module, "printf", printf_type);

    LLVMTypeRef main_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
    LLVMValueRef main_func = LLVMAddFunction(module, "main", main_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    CodegenCtx ctx = { module, builder, printf_func, printf_type, NULL };
    codegen_node(&ctx, root);

    LLVMBuildRet(builder, LLVMConstInt(LLVMInt32Type(), 0, 0));
    LLVMDisposeBuilder(builder);
    
    Symbol *s = ctx.symbols;
    while(s) { Symbol *next = s->next; free(s->name); free(s); s = next; }

    return module;
}
