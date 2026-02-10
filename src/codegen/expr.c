#include "codegen.h"
#include "../diagnostic/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward decl
LLVMValueRef generate_enum_to_string_func(CodegenCtx *ctx, EnumInfo *ei);

char* format_string(const char* input) {
  if (!input) return NULL;
  size_t len = strlen(input);
  char *new_str = malloc(len + 1);
  strcpy(new_str, input);
  return new_str;
}

// Helper to stringify type for typeof
static void get_type_name(VarType t, char *buf) {
    switch (t.base) {
        case TYPE_INT: strcpy(buf, "int"); break;
        case TYPE_CHAR: strcpy(buf, "char"); break;
        case TYPE_BOOL: strcpy(buf, "bool"); break;
        case TYPE_FLOAT: strcpy(buf, "float"); break;
        case TYPE_DOUBLE: strcpy(buf, "double"); break;
        case TYPE_VOID: strcpy(buf, "void"); break;
        case TYPE_STRING: strcpy(buf, "string"); break;
        case TYPE_CLASS: 
            strcpy(buf, t.class_name ? t.class_name : "object"); 
            break;
        default: strcpy(buf, "unknown"); break;
    }
    
    // Pointers
    for(int i=0; i<t.ptr_depth; i++) strcat(buf, "*");
    
    // Arrays
    if (t.array_size > 0) {
        char tmp[32];
        sprintf(tmp, "[%d]", t.array_size);
        strcat(buf, tmp);
    }
}

// Calculate the Alkyl VarType of an expression (for type checking/inference in codegen)
VarType codegen_calc_type(CodegenCtx *ctx, ASTNode *node) {
    VarType vt = {TYPE_UNKNOWN, 0, NULL};
    if (!node) return vt;
    
    if (node->type == NODE_LITERAL) return ((LiteralNode*)node)->var_type;
    
    if (node->type == NODE_VAR_REF) {
        Symbol *s = find_symbol(ctx, ((VarRefNode*)node)->name);
        if (s) return s->vtype;
        
        // Check Enum Type
        EnumInfo *ei = find_enum(ctx, ((VarRefNode*)node)->name);
        if (ei) return (VarType){TYPE_INT, 0, NULL}; // Placeholder type for the Enum Type itself

        // Implicit 'this' member type lookup
        Symbol *this_sym = find_symbol(ctx, "this");
        if (this_sym && this_sym->vtype.class_name) {
             ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
             if (ci) {
                 VarType mvt;
                 if (get_member_index(ci, ((VarRefNode*)node)->name, NULL, &mvt) != -1) {
                     return mvt;
                 }
             }
        }
    }
    
    if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *aa = (ArrayAccessNode*)node;
        
        // Check Enum lookup
        if (aa->target->type == NODE_VAR_REF) {
            EnumInfo *ei = find_enum(ctx, ((VarRefNode*)aa->target)->name);
            if (ei) return (VarType){TYPE_STRING, 0, NULL};
        }

        VarType t = codegen_calc_type(ctx, aa->target);
        if (t.ptr_depth > 0) t.ptr_depth--;
        else if (t.array_size > 0) { t.array_size = 0; } // Decay array to element
        return t;
    }

    if (node->type == NODE_TRAIT_ACCESS) {
        TraitAccessNode *ta = (TraitAccessNode*)node;
        VarType t = codegen_calc_type(ctx, ta->object);
        if (t.base == TYPE_CLASS) {
            // Returns TraitType* (same ptr depth as object)
            t.class_name = strdup(ta->trait_name);
            // Trait access always produces a pointer (view/GEP) to the trait part
            if (t.ptr_depth == 0) t.ptr_depth = 1;
            return t;
        }
    }

    if (node->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)node;
        
        // Fix for Enum Member Type Inference (e.g. Makanan.Ayam should be int)
        if (ma->object->type == NODE_VAR_REF) {
            EnumInfo *ei = find_enum(ctx, ((VarRefNode*)ma->object)->name);
            if (ei) return (VarType){TYPE_INT, 0, NULL};
        }

        VarType obj_t = codegen_calc_type(ctx, ma->object);
        if (obj_t.base == TYPE_CLASS && obj_t.class_name) {
            ClassInfo *ci = find_class(ctx, obj_t.class_name);
            if (ci) {
                 VarType mvt;
                 if (get_member_index(ci, ma->member_name, NULL, &mvt) != -1) {
                     return mvt;
                 }
            }
        }
    }
    
    if (node->type == NODE_CALL) {
        CallNode *call = (CallNode*)node;
        const char *name = call->mangled_name ? call->mangled_name : call->name;
        
        // 1. Check Function Symbol
        FuncSymbol *fs = find_func_symbol(ctx, name);
        if (fs) return fs->ret_type;
        
        // 2. Check Class Constructor
        ClassInfo *ci = find_class(ctx, call->name); // Use original name for class lookup
        if (ci) {
            vt.base = TYPE_CLASS;
            vt.class_name = strdup(call->name);
            // FIX: Constructor returns a pointer to the object on heap
            vt.ptr_depth = 1; 
            return vt;
        }

        // 3. Check Builtins
        if (strcmp(name, "input") == 0) { vt.base = TYPE_STRING; return vt; }
    }

    if (node->type == NODE_METHOD_CALL) {
        MethodCallNode *mc = (MethodCallNode*)node;
        if (mc->mangled_name) {
             FuncSymbol *fs = find_func_symbol(ctx, mc->mangled_name);
             if (fs) return fs->ret_type;
        }
    }
    
    if (node->type == NODE_TYPEOF) {
        return (VarType){TYPE_STRING, 0, NULL};
    }
    
    return vt;
}

// Generate the memory address (l-value) of an expression
LLVMValueRef codegen_addr(CodegenCtx *ctx, ASTNode *node) {
     if (node->type == NODE_VAR_REF) {
        VarRefNode *r = (VarRefNode*)node;
        Symbol *sym = find_symbol(ctx, r->name);
        
        // If it is a direct value (enum constant), it has no address
        if (sym && sym->is_direct_value) return NULL;
        
        if (sym) return sym->value;
        
        // Check for implicit 'this' member access
        Symbol *this_sym = find_symbol(ctx, "this");
        if (this_sym && this_sym->vtype.class_name) {
             ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
             if (ci) {
                 int idx = get_member_index(ci, r->name, NULL, NULL);
                 if (idx != -1) {
                     LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder, this_sym->type, this_sym->value, "this_ptr");
                     LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                     return LLVMBuildGEP2(ctx->builder, ci->struct_type, this_val, indices, 2, "implicit_mem_ptr");
                 }
             }
        }
     }
     
     if (node->type == NODE_ARRAY_ACCESS) {
         ArrayAccessNode *aa = (ArrayAccessNode*)node;
         
         // Enum lookup is R-Value
         if (aa->target->type == NODE_VAR_REF) {
              if (find_enum(ctx, ((VarRefNode*)aa->target)->name)) return NULL;
         }

         LLVMValueRef target = codegen_addr(ctx, aa->target);
         if (!target) target = codegen_expr(ctx, aa->target); // Might be a pointer r-value
         
         LLVMValueRef index = codegen_expr(ctx, aa->index);
         VarType t = codegen_calc_type(ctx, aa->target);
         LLVMTypeRef el_type = get_llvm_type(ctx, t);
         
         if (t.ptr_depth > 0) {
             // Pointer indexing
             LLVMValueRef base = LLVMBuildLoad2(ctx->builder, el_type, target, "ptr_base");
             // Decrease depth for element type
             t.ptr_depth--;
             LLVMTypeRef inner_type = get_llvm_type(ctx, t);
             return LLVMBuildGEP2(ctx->builder, inner_type, base, &index, 1, "ptr_idx");
         } else if (t.array_size > 0) {
             // Array indexing
             LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), index };
             return LLVMBuildGEP2(ctx->builder, el_type, target, indices, 2, "arr_idx");
         }
     }

     if (node->type == NODE_MEMBER_ACCESS) {
         MemberAccessNode *ma = (MemberAccessNode*)node;
         
         // Check Enum Member Access (R-Value)
         if (ma->object->type == NODE_VAR_REF) {
              if (find_enum(ctx, ((VarRefNode*)ma->object)->name)) return NULL;
         }

         // Try getting address of object (L-value)
         LLVMValueRef obj = codegen_addr(ctx, ma->object);
         
         VarType obj_t = codegen_calc_type(ctx, ma->object);
         if (obj_t.base == TYPE_CLASS && obj_t.class_name) {
             ClassInfo *ci = find_class(ctx, obj_t.class_name);
             if (ci) {
                 int idx = get_member_index(ci, ma->member_name, NULL, NULL);
                 if (idx != -1) {
                     if (obj_t.ptr_depth > 0) {
                        // Obj is a pointer (e.g., 'this', or Class* ptr)
                        LLVMValueRef base;
                        if (obj) {
                            base = LLVMBuildLoad2(ctx->builder, LLVMPointerType(ci->struct_type, 0), obj, "obj_ptr");
                        } else {
                            // Fallback: Object is R-value pointer (e.g. Faran[Hand])
                            base = codegen_expr(ctx, ma->object);
                        }
                        
                        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                        return LLVMBuildGEP2(ctx->builder, ci->struct_type, base, indices, 2, "mem_ptr");
                     } else {
                        // Obj is a struct value on stack (L-value must exist)
                        if (!obj) {
                             char msg[256];
                             snprintf(msg, 256, "Cannot access member '%s' of r-value struct", ma->member_name);
                             codegen_error(ctx, node, msg);
                        }
                        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                        return LLVMBuildGEP2(ctx->builder, ci->struct_type, obj, indices, 2, "mem_ptr");
                     }
                 }
             }
         }
     }
     
     return NULL;
}

LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node) {
  if (!node) return LLVMConstInt(LLVMInt32Type(), 0, 0);

  if (node->type == NODE_CALL) {
    CallNode *c = (CallNode*)node;
    ClassInfo *ci = find_class(ctx, c->name);
    if (ci) {
        // Constructor: Allocate struct size
        LLVMValueRef size = LLVMSizeOf(ci->struct_type);
        LLVMValueRef mem = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "new_obj");
        LLVMValueRef obj = LLVMBuildBitCast(ctx->builder, mem, LLVMPointerType(ci->struct_type, 0), "obj_cast");
        
        // Initialize members with arguments
        ASTNode *arg = c->args;
        ClassMember *m = ci->members;
        
        while (arg && m) {
            LLVMValueRef arg_val = codegen_expr(ctx, arg);
            
            // GEP to member
            LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), m->index, 0) };
            LLVMValueRef mem_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, obj, indices, 2, "init_mem_ptr");
            
            // Handle String Array Initialization (basic strcpy)
            if (m->vtype.array_size > 0 && m->vtype.base == TYPE_CHAR) {
                 LLVMValueRef dest = LLVMBuildBitCast(ctx->builder, mem_ptr, LLVMPointerType(LLVMInt8Type(), 0), "dest_cast");
                 LLVMValueRef src = arg_val;
                 // If src is i8* (decayed string) and dest is i8*, we can copy
                 // NOTE: arg_val MUST be i8*, so codegen_expr of array must return decayed pointer
                 LLVMValueRef args[] = { dest, src };
                 LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args, 2, "");
            } else {
                 LLVMBuildStore(ctx->builder, arg_val, mem_ptr);
            }
            
            arg = arg->next;
            m = m->next;
        }

        return obj;
    }
    
    // Builtins
    if (strcmp(c->name, "print") == 0 || strcmp(c->name, "printf") == 0) {
      int arg_count = 0; ASTNode *curr = c->args; while(curr) { arg_count++; curr = curr->next; }
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
      curr = c->args; for(int i=0; i<arg_count; i++) { args[i] = codegen_expr(ctx, curr); curr = curr->next; }
      LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, args, arg_count, "");
      free(args); return ret;
    }
    
    if (strcmp(c->name, "input") == 0) {
        return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->input_func), ctx->input_func, NULL, 0, "user_input");
    }
    
    if (strcmp(c->name, "malloc") == 0 || strcmp(c->name, "alloc") == 0) {
        LLVMValueRef size = codegen_expr(ctx, c->args);
        return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->malloc_func), ctx->malloc_func, &size, 1, "malloc_res");
    }
    
    if (strcmp(c->name, "free") == 0) {
        LLVMValueRef ptr = codegen_expr(ctx, c->args);
        LLVMValueRef args[] = { LLVMBuildBitCast(ctx->builder, ptr, LLVMPointerType(LLVMInt8Type(), 0), "free_cast") };
        return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->free_func), ctx->free_func, args, 1, "");
    }

    // Function Call with Overload Support
    const char *target_name = c->mangled_name ? c->mangled_name : c->name;
    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, target_name);
    
    if (!func) {
        // If not found by mangled name, try raw name (for externs)
        func = LLVMGetNamedFunction(ctx->module, c->name);
        if (!func) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Undefined function '%s'.", c->name);
            codegen_error(ctx, node, msg);
        }
    }
    
    FuncSymbol *fsym = find_func_symbol(ctx, target_name);
    if (!fsym) fsym = find_func_symbol(ctx, c->name);
    
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
  else if (node->type == NODE_METHOD_CALL) {
      MethodCallNode *mc = (MethodCallNode*)node;

      // 0. Handle Static/Namespace Calls
      if (mc->is_static) {
          if (!mc->mangled_name) codegen_error(ctx, node, "Static call missing mangled name");
          
          LLVMValueRef func = LLVMGetNamedFunction(ctx->module, mc->mangled_name);
          if (!func) codegen_error(ctx, node, "Static function not found in module");
          
          int arg_count = 0;
          ASTNode *arg = mc->args;
          while(arg) { arg_count++; arg = arg->next; }
          
          LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
          arg = mc->args;
          for(int i=0; i<arg_count; i++) {
              args[i] = codegen_expr(ctx, arg);
              arg = arg->next;
          }
          
          LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
          LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count, "");
          free(args);
          return ret;
      }
      
      // 1. Get object pointer
      LLVMValueRef obj_ptr = NULL;
      VarType obj_t = codegen_calc_type(ctx, mc->object);
      
      if (obj_t.ptr_depth > 0) obj_ptr = codegen_expr(ctx, mc->object);
      else obj_ptr = codegen_addr(ctx, mc->object);
      
      if (!obj_ptr) codegen_error(ctx, node, "Invalid object for method call");
      
      // 2. Adjust 'this' pointer if method belongs to parent or trait
      if (mc->owner_class && obj_t.class_name && strcmp(mc->owner_class, obj_t.class_name) != 0) {
          ClassInfo *ci = find_class(ctx, obj_t.class_name);
          
          // Check for Trait Offset
          int offset = get_trait_offset(ctx, ci, mc->owner_class);
          if (offset != -1) {
              // GEP to trait
              LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), offset, 0) };
              obj_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, obj_ptr, indices, 2, "trait_this_adj");
          } else {
              // Assume Parent -> BitCast
              ClassInfo *target_cls = find_class(ctx, mc->owner_class);
              if (target_cls) {
                  obj_ptr = LLVMBuildBitCast(ctx->builder, obj_ptr, LLVMPointerType(target_cls->struct_type, 0), "parent_this_cast");
              }
          }
      }
      
      // 3. Resolve Function
      if (!mc->mangled_name) {
          char msg[256];
          snprintf(msg, 256, "Method '%s' resolution failed (mangled name is null).", mc->method_name);
          codegen_error(ctx, node, msg);
      }

      LLVMValueRef func = LLVMGetNamedFunction(ctx->module, mc->mangled_name);
      
      // Fallback: Try "Class_Method" if mangled name not found (e.g. definition used simple naming)
      if (!func && mc->owner_class) {
           char fallback[256];
           snprintf(fallback, sizeof(fallback), "%s_%s", mc->owner_class, mc->method_name);
           func = LLVMGetNamedFunction(ctx->module, fallback);
      }

      if (!func) {
          char msg[256];
          snprintf(msg, 256, "Linker Error: Method '%s' not found.", mc->mangled_name);
          codegen_error(ctx, node, msg);
      }
      
      // 4. Build Args (Arg 0 is this)
      int arg_count = 0;
      ASTNode *arg = mc->args;
      while(arg) { arg_count++; arg = arg->next; }
      
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * (arg_count + 1));
      args[0] = obj_ptr; // 'this'
      
      arg = mc->args;
      for(int i=0; i<arg_count; i++) {
          args[i+1] = codegen_expr(ctx, arg);
          arg = arg->next;
      }
      
      LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
      LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count + 1, "");
      free(args);
      return ret;
  }
  else if (node->type == NODE_TRAIT_ACCESS) {
      TraitAccessNode *ta = (TraitAccessNode*)node;
      
      // Get base pointer
      LLVMValueRef obj_ptr = NULL;
      VarType obj_t = codegen_calc_type(ctx, ta->object);
      
      if (obj_t.ptr_depth > 0) {
          // Object is a pointer expression
          obj_ptr = codegen_expr(ctx, ta->object);
      } else {
          // Object is L-value, get address
          obj_ptr = codegen_addr(ctx, ta->object);
      }
      
      if (!obj_ptr) codegen_error(ctx, node, "Cannot access trait of null/invalid object");

      if (!obj_t.class_name) codegen_error(ctx, node, "Object type has no class name");
      
      ClassInfo *ci = find_class(ctx, obj_t.class_name);
      if (!ci) codegen_error(ctx, node, "Object is not a class");

      // FIX: pass ctx to prevent crash when looking up parents
      int offset = get_trait_offset(ctx, ci, ta->trait_name);
      if (offset == -1) {
          char msg[256]; snprintf(msg, 256, "Class '%s' does not have trait '%s'", ci->name, ta->trait_name);
          codegen_error(ctx, node, msg);
      }
      
      ClassInfo *trait = find_class(ctx, ta->trait_name);
      LLVMTypeRef trait_type = trait ? trait->struct_type : LLVMStructCreateNamed(LLVMGetGlobalContext(), ta->trait_name);
      
      // GEP to sub-object
      // Note: If offset is 0, we can just cast. 
      // But GEP is safer for types.
      LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), offset, 0) };
      LLVMValueRef trait_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, obj_ptr, indices, 2, "trait_ptr");
      
      return LLVMBuildBitCast(ctx->builder, trait_ptr, LLVMPointerType(trait_type, 0), "trait_cast");
  }
  else if (node->type == NODE_LITERAL) {
    LiteralNode *l = (LiteralNode*)node;
    
    // FIX: Only treat as string if the type says so.
    // Accessing .str_val on a numeric literal (union) causes Segfault in strlen()
    int is_string = (l->var_type.base == TYPE_STRING) || 
                    (l->var_type.base == TYPE_CHAR && l->var_type.ptr_depth > 0);

    if (is_string) {
        if (l->val.str_val) {
          char *fmt = format_string(l->val.str_val);
          LLVMValueRef gstr = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "str_lit");
          free(fmt);
          return gstr;
        } else {
            return LLVMConstPointerNull(LLVMPointerType(LLVMInt8Type(), 0));
        }
    }

    if (l->var_type.base == TYPE_DOUBLE) return LLVMConstReal(LLVMDoubleType(), l->val.double_val);
    if (l->var_type.base == TYPE_BOOL) return LLVMConstInt(LLVMInt1Type(), l->val.int_val, 0);
    if (l->var_type.base == TYPE_CHAR) return LLVMConstInt(LLVMInt8Type(), l->val.int_val, 0); 
    
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
      // Relational Ops
      if (op->op == TOKEN_LT) return LLVMBuildICmp(ctx->builder, LLVMIntSLT, l, r, "lt");
      if (op->op == TOKEN_GT) return LLVMBuildICmp(ctx->builder, LLVMIntSGT, l, r, "gt");
      if (op->op == TOKEN_LTE) return LLVMBuildICmp(ctx->builder, LLVMIntSLE, l, r, "lte");
      if (op->op == TOKEN_GTE) return LLVMBuildICmp(ctx->builder, LLVMIntSGE, l, r, "gte");
      if (op->op == TOKEN_EQ) return LLVMBuildICmp(ctx->builder, LLVMIntEQ, l, r, "eq");
      if (op->op == TOKEN_NEQ) return LLVMBuildICmp(ctx->builder, LLVMIntNE, l, r, "neq");

      return LLVMConstInt(LLVMInt32Type(), 0, 0);
  }
  else if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *aa = (ArrayAccessNode*)node;
        VarType target_t = codegen_calc_type(ctx, aa->target);

        // Check Enum String Lookup (e.g. Makanan[sarapan])
        if (aa->target->type == NODE_VAR_REF) {
             const char *name = ((VarRefNode*)aa->target)->name;
             EnumInfo *ei = find_enum(ctx, name);
             if (ei) {
                 LLVMValueRef idx = codegen_expr(ctx, aa->index);
                 if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
                    idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt32Type(), "idx_cast");
                 } else {
                    idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt32Type(), "idx_cast");
                 }
                 
                 return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ei->to_string_func), ei->to_string_func, &idx, 1, "enum_str");
             }
        }
        
        // String Indexing
        if (target_t.base == TYPE_STRING) {
             LLVMValueRef target = codegen_expr(ctx, aa->target);
             LLVMValueRef index = codegen_expr(ctx, aa->index);
             LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, LLVMInt8Type(), target, &index, 1, "str_idx");
             return LLVMBuildLoad2(ctx->builder, LLVMInt8Type(), gep, "char_val");
        }
        
        LLVMValueRef target = codegen_addr(ctx, aa->target);
        if(!target) target = codegen_expr(ctx, aa->target); 
        LLVMValueRef index = codegen_expr(ctx, aa->index);
        LLVMTypeRef el_type = get_llvm_type(ctx, target_t); 
        
        // Handle pointer vs array
        if (target_t.ptr_depth > 0) {
            el_type = LLVMGetElementType(el_type); // deref the pointer type for GEP
            LLVMValueRef base = LLVMBuildLoad2(ctx->builder, el_type, target, "ptr_base");
            LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, LLVMGetElementType(el_type), base, &index, 1, "ptr_idx");
            return LLVMBuildLoad2(ctx->builder, LLVMGetElementType(el_type), gep, "val");
        } else {
             LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), index };
             LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, el_type, target, indices, 2, "arr_idx");
             return LLVMBuildLoad2(ctx->builder, LLVMGetElementType(el_type), gep, "val");
        }
  }
  else if (node->type == NODE_MEMBER_ACCESS) {
      MemberAccessNode *ma = (MemberAccessNode*)node;
      
      // Check Enum Member Access (Enum.Member)
      if (ma->object->type == NODE_VAR_REF) {
           const char *obj_name = ((VarRefNode*)ma->object)->name;
           EnumInfo *ei = find_enum(ctx, obj_name);
           if (ei) {
               // Find member value
               EnumEntryInfo *curr = ei->entries;
               while(curr) {
                   if (strcmp(curr->name, ma->member_name) == 0) {
                       return LLVMConstInt(LLVMInt32Type(), curr->value, 0);
                   }
                   curr = curr->next;
               }
               // This should be unreachable if semantic analysis passes, but safe to error
               codegen_error(ctx, node, "Enum member not found in codegen");
           }
      }

      // For R-values, usually we load from address
      LLVMValueRef addr = codegen_addr(ctx, node);
      if (addr) {
          VarType vt = codegen_calc_type(ctx, node);
          
          // Fix for Array Decay: Return pointer to start instead of loading aggregate
          if (vt.array_size > 0 && vt.ptr_depth == 0) {
               LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
               return LLVMBuildGEP2(ctx->builder, get_llvm_type(ctx, vt), addr, indices, 2, "mem_decay");
          }

          LLVMTypeRef type = get_llvm_type(ctx, vt);
          return LLVMBuildLoad2(ctx->builder, type, addr, "mem_val");
      }
  }
  else if (node->type == NODE_INC_DEC) {
      IncDecNode *id = (IncDecNode*)node;
      
      // 1. Get Address
      LLVMValueRef addr = codegen_addr(ctx, id->target);
      if (!addr) {
          codegen_error(ctx, node, "Operand must be an l-value");
      }
      
      // 2. Load Old Value
      VarType vt = codegen_calc_type(ctx, id->target);
      LLVMTypeRef type = get_llvm_type(ctx, vt);
      LLVMValueRef old_val = LLVMBuildLoad2(ctx->builder, type, addr, "old_val");
      
      // 3. Compute New Value
      LLVMValueRef new_val = NULL;
      LLVMValueRef one = NULL;
      
      int is_float = (vt.base == TYPE_FLOAT || vt.base == TYPE_DOUBLE);
      
      if (is_float) {
          one = LLVMConstReal(type, 1.0);
          if (id->op == TOKEN_INCREMENT) 
              new_val = LLVMBuildFAdd(ctx->builder, old_val, one, "new_val");
          else 
              new_val = LLVMBuildFSub(ctx->builder, old_val, one, "new_val");
      } else {
          one = LLVMConstInt(type, 1, 0);
          if (id->op == TOKEN_INCREMENT) 
              new_val = LLVMBuildAdd(ctx->builder, old_val, one, "new_val");
          else 
              new_val = LLVMBuildSub(ctx->builder, old_val, one, "new_val");
      }
      
      // 4. Store New Value
      LLVMBuildStore(ctx->builder, new_val, addr);
      
      // 5. Return Result
      return id->is_prefix ? new_val : old_val;
  }
  else if (node->type == NODE_VAR_REF) {
      const char *name = ((VarRefNode*)node)->name;
      Symbol *sym = find_symbol(ctx, name);
      if (sym) {
          // Direct Value (Enum Constant)
          if (sym->is_direct_value) {
              return sym->value;
          }

          // Fix for Array Decay: Return pointer to start instead of loading aggregate
          if (sym->vtype.array_size > 0 && sym->vtype.ptr_depth == 0) {
              LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
              return LLVMBuildGEP2(ctx->builder, get_llvm_type(ctx, sym->vtype), sym->value, indices, 2, "arr_decay");
          }
          return LLVMBuildLoad2(ctx->builder, sym->type, sym->value, sym->name);
      }
      
      // Implicit 'this' Member Access
      Symbol *this_sym = find_symbol(ctx, "this");
      if (this_sym && this_sym->vtype.class_name) {
           ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
           if (ci) {
               LLVMTypeRef mem_type;
               VarType mvt;
               int idx = get_member_index(ci, name, &mem_type, &mvt);
               
               if (idx != -1) {
                   LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder, this_sym->type, this_sym->value, "this_ptr");
                   LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                   LLVMValueRef mem_ptr = LLVMBuildGEP2(ctx->builder, ci->struct_type, this_val, indices, 2, "implicit_mem_ptr");
                   
                   // Handle Array Decay for Members
                   if (mvt.array_size > 0 && mvt.ptr_depth == 0) {
                       LLVMValueRef arr_indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
                       return LLVMBuildGEP2(ctx->builder, mem_type, mem_ptr, arr_indices, 2, "mem_arr_decay");
                   }

                   return LLVMBuildLoad2(ctx->builder, mem_type, mem_ptr, name);
               }
           }
      }

      char msg[256];
      snprintf(msg, sizeof(msg), "Codegen Error: Undefined variable '%s'", name);
      codegen_error(ctx, node, msg);
  }
  else if (node->type == NODE_TYPEOF) {
      UnaryOpNode *tn = (UnaryOpNode*)node;
      VarType t = codegen_calc_type(ctx, tn->operand);
      
      char buf[256];
      get_type_name(t, buf);
      
      return LLVMBuildGlobalStringPtr(ctx->builder, buf, "typeof_str");
  }
  
  return LLVMConstInt(LLVMInt32Type(), 0, 0);
}
