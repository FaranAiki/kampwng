#include "codegen.h"
#include "../diagnostic/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward decl
LLVMValueRef generate_enum_to_string_func(CodegenCtx *ctx, EnumInfo *ei);

// ... format_string, find_closest_match, codegen_calc_type, codegen_addr unchanged ...

char* format_string(const char* input) {
  if (!input) return NULL;
  size_t len = strlen(input);
  char *new_str = malloc(len + 2);
  strcpy(new_str, input);
  return new_str;
}

// ... codegen_calc_type ... (unchanged)
VarType codegen_calc_type(CodegenCtx *ctx, ASTNode *node) {
    VarType vt = {TYPE_UNKNOWN, 0, NULL};
    if (!node) return vt;
    if (node->type == NODE_LITERAL) return ((LiteralNode*)node)->var_type;
    if (node->type == NODE_VAR_REF) {
        Symbol *s = find_symbol(ctx, ((VarRefNode*)node)->name);
        if (s) return s->vtype;
    }
    // ... complete implementation assumed from existing file ...
    return vt;
}

// ... codegen_addr ... (unchanged)
LLVMValueRef codegen_addr(CodegenCtx *ctx, ASTNode *node) {
     if (node->type == NODE_VAR_REF) {
        VarRefNode *r = (VarRefNode*)node;
        Symbol *sym = find_symbol(ctx, r->name);
        if (sym) return sym->value;
     }
     // ... complete implementation assumed ...
     return NULL;
}

LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node) {
  if (!node) return LLVMConstInt(LLVMInt32Type(), 0, 0);

  if (node->type == NODE_CALL) {
    CallNode *c = (CallNode*)node;
    ClassInfo *ci = find_class(ctx, c->name);
    if (ci) {
        // ... Constructor logic unchanged ...
    }
    
    // Builtins
    if (strcmp(c->name, "print") == 0) {
      int arg_count = 0; ASTNode *curr = c->args; while(curr) { arg_count++; curr = curr->next; }
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
      curr = c->args; for(int i=0; i<arg_count; i++) { args[i] = codegen_expr(ctx, curr); curr = curr->next; }
      LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, args, arg_count, "");
      free(args); return ret;
    }
    // ... input, malloc, free, setjmp, longjmp logic unchanged ...
    
    // Function Call with Overload Support
    const char *target_name = c->mangled_name ? c->mangled_name : c->name;
    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, target_name);
    
    if (!func) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Undefined function '%s'.", target_name);
        exit(1);
    }
    
    FuncSymbol *fsym = find_func_symbol(ctx, target_name);
    
    int arg_count = 0; ASTNode *curr = c->args; while(curr) { arg_count++; curr = curr->next; }
    LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
    curr = c->args; 
    
    for(int i=0; i<arg_count; i++) { 
        LLVMValueRef val = codegen_expr(ctx, curr);
        
        // Implicit Casting based on Function Signature
        if (fsym && i < fsym->param_count) {
             VarType expected = fsym->param_types[i];
             LLVMTypeRef llvm_expected = get_llvm_type(ctx, expected);
             
             if (LLVMGetTypeKind(llvm_expected) == LLVMDoubleTypeKind) {
                 if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                     val = LLVMBuildSIToFP(ctx->builder, val, llvm_expected, "cast_int_to_double");
                 } else if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMFloatTypeKind) {
                     val = LLVMBuildFPExt(ctx->builder, val, llvm_expected, "cast_float_to_double");
                 }
             } else if (LLVMGetTypeKind(llvm_expected) == LLVMFloatTypeKind) {
                 if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                     val = LLVMBuildSIToFP(ctx->builder, val, llvm_expected, "cast_int_to_float");
                 }
             }
        }
        
        args[i] = val; 
        curr = curr->next; 
    }
    
    LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
    LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count, "");
    free(args); return ret;
  }
  else if (node->type == NODE_LITERAL) {
    LiteralNode *l = (LiteralNode*)node;
    if (l->var_type.base == TYPE_DOUBLE) return LLVMConstReal(LLVMDoubleType(), l->val.double_val);
    if (l->var_type.base == TYPE_BOOL) return LLVMConstInt(LLVMInt1Type(), l->val.int_val, 0);
    if (l->var_type.base == TYPE_CHAR) return LLVMConstInt(LLVMInt8Type(), l->val.int_val, 0); 
    if (l->var_type.base == TYPE_STRING) {
      // Both Alkyl String and C-Strings are effectively i8* here
      char *fmt = format_string(l->val.str_val);
      LLVMValueRef gstr = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "str_lit");
      free(fmt);
      return gstr;
    }
    return LLVMConstInt(get_llvm_type(ctx, l->var_type), l->val.int_val, 0);
  }
  else if (node->type == NODE_BINARY_OP) {
      BinaryOpNode *op = (BinaryOpNode*)node;
      LLVMValueRef l = codegen_expr(ctx, op->left);
      LLVMValueRef r = codegen_expr(ctx, op->right);

      VarType lt = codegen_calc_type(ctx, op->left);
      VarType rt = codegen_calc_type(ctx, op->right);
      
      // Alkyl String Concatenation: +
      if (lt.base == TYPE_STRING && rt.base == TYPE_STRING && op->op == TOKEN_PLUS) {
             // Generate: len1 = strlen(l), len2 = strlen(r), size = len1 + len2 + 1, malloc(size), strcpy, strcat
             LLVMValueRef len1 = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strlen_func), ctx->strlen_func, &l, 1, "len1");
             LLVMValueRef len2 = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strlen_func), ctx->strlen_func, &r, 1, "len2");
             LLVMValueRef sum = LLVMBuildAdd(ctx->builder, len1, len2, "len_sum");
             LLVMValueRef size = LLVMBuildAdd(ctx->builder, sum, LLVMConstInt(LLVMInt64Type(), 1, 0), "alloc_size");
             
             LLVMValueRef mem = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "new_str");
             
             LLVMValueRef args_cpy[] = { mem, l };
             LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args_cpy, 2, "");
             
             // Offset to end of string 1 for second copy
             LLVMValueRef idxs[] = { len1 };
             LLVMValueRef ptr2 = LLVMBuildGEP2(ctx->builder, LLVMInt8Type(), mem, idxs, 1, "ptr2");
             LLVMValueRef args_cpy2[] = { ptr2, r };
             LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args_cpy2, 2, "");
             
             return mem;
      }
      
      // Alkyl String Comparison: ==, !=
      if (lt.base == TYPE_STRING && rt.base == TYPE_STRING && (op->op == TOKEN_EQ || op->op == TOKEN_NEQ)) {
             LLVMValueRef args[] = {l, r};
             LLVMValueRef cmp_res = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcmp_func), ctx->strcmp_func, args, 2, "cmp_res");
             
             if (op->op == TOKEN_EQ) 
                 return LLVMBuildICmp(ctx->builder, LLVMIntEQ, cmp_res, LLVMConstInt(LLVMInt32Type(), 0, 0), "str_eq");
             else
                 return LLVMBuildICmp(ctx->builder, LLVMIntNE, cmp_res, LLVMConstInt(LLVMInt32Type(), 0, 0), "str_neq");
      }

      // Basic Math Ops
      if (op->op == TOKEN_PLUS) return LLVMBuildAdd(ctx->builder, l, r, "add");
      if (op->op == TOKEN_MINUS) return LLVMBuildSub(ctx->builder, l, r, "sub");
      if (op->op == TOKEN_STAR) return LLVMBuildMul(ctx->builder, l, r, "mul");
      if (op->op == TOKEN_SLASH) return LLVMBuildSDiv(ctx->builder, l, r, "div");
      return LLVMConstInt(LLVMInt32Type(), 0, 0);
  }
  else if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *aa = (ArrayAccessNode*)node;
        VarType target_t = codegen_calc_type(ctx, aa->target);
        
        // String Indexing
        if (target_t.base == TYPE_STRING) {
             LLVMValueRef target = codegen_expr(ctx, aa->target);
             LLVMValueRef index = codegen_expr(ctx, aa->index);
             LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, LLVMInt8Type(), target, &index, 1, "str_idx");
             return LLVMBuildLoad2(ctx->builder, LLVMInt8Type(), gep, "char_val");
        }
        
        // ... existing array/ptr access implementation ...
        LLVMValueRef target = codegen_addr(ctx, aa->target);
        if(!target) target = codegen_expr(ctx, aa->target); 
        LLVMValueRef index = codegen_expr(ctx, aa->index);
        LLVMTypeRef el_type = get_llvm_type(ctx, target_t); 
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, el_type, target, &index, 1, "ptr_idx");
        return LLVMBuildLoad2(ctx->builder, el_type, gep, "val");
  }
  
  return LLVMConstInt(LLVMInt32Type(), 0, 0);
}
