#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* format_string(const char* input) {
  if (!input) return NULL;
  size_t len = strlen(input);
  char *new_str = malloc(len + 2);
  strcpy(new_str, input);
  return new_str;
}

// ... codegen_calc_type ... 
VarType codegen_calc_type(CodegenCtx *ctx, ASTNode *node) {
    VarType vt = {TYPE_UNKNOWN, 0, NULL};
    if (!node) return vt;
    if (node->type == NODE_LITERAL) {
        return ((LiteralNode*)node)->var_type;
    } 
    else if (node->type == NODE_VAR_REF) {
        Symbol *s = find_symbol(ctx, ((VarRefNode*)node)->name);
        if (s) return s->vtype;
        Symbol *this_sym = find_symbol(ctx, "this");
        if (this_sym && this_sym->vtype.class_name) {
            ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
            VarType mvt;
            if (get_member_index(ci, ((VarRefNode*)node)->name, NULL, &mvt) != -1) return mvt;
        }
    }
    else if (node->type == NODE_UNARY_OP) {
        UnaryOpNode *u = (UnaryOpNode*)node;
        if (u->base.type == NODE_TYPEOF) { vt.base = TYPE_STRING; return vt; }
        VarType t = codegen_calc_type(ctx, u->operand);
        if (u->op == TOKEN_STAR) { if (t.ptr_depth > 0) t.ptr_depth--; return t; }
        else if (u->op == TOKEN_AND) { t.ptr_depth++; return t; }
        return t; 
    }
    else if (node->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)node;
        
        // Check for Namespace Access
        if (ma->object->type == NODE_VAR_REF) {
            char *ns_name = ((VarRefNode*)ma->object)->name;
            if (is_namespace(ctx, ns_name)) {
                // Resolution: This is a namespace access
                // We assume user is accessing a function call, handled in NODE_CALL/NODE_EXPR
                // But if they are accessing a variable inside namespace, handle here?
                // Standard approach: Return unknown here, let codegen_expr resolve if value needed.
                // However, calc_type needs to know return type.
                // We'd need to look up function symbol with mangled name.
                char mangled[256];
                sprintf(mangled, "%s_%s", ns_name, ma->member_name);
                FuncSymbol *fs = find_func_symbol(ctx, mangled);
                if (fs) return fs->ret_type;
                // Variable?
                Symbol *s = find_symbol(ctx, mangled);
                if (s) return s->vtype;
            }
        }

        VarType obj_type = codegen_calc_type(ctx, ma->object);
        while (obj_type.ptr_depth > 0) obj_type.ptr_depth--;
        if (obj_type.base == TYPE_CLASS && obj_type.class_name) {
            ClassInfo *ci = find_class(ctx, obj_type.class_name);
            if (ci) {
                VarType mem_vt;
                if (get_member_index(ci, ma->member_name, NULL, &mem_vt) != -1) return mem_vt;
            }
        }
        return vt;
    }
    else if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *an = (ArrayAccessNode*)node;
        VarType t = codegen_calc_type(ctx, an->target);
        if (t.array_size > 0) {
             t.array_size = 0; 
             return t;
        }
        if (t.ptr_depth > 0) {
            t.ptr_depth--;
            return t;
        }
        return t;
    }
    else if (node->type == NODE_TRAIT_ACCESS) {
        TraitAccessNode *ta = (TraitAccessNode*)node;
        vt.base = TYPE_CLASS; vt.class_name = strdup(ta->trait_name); return vt;
    }
    else if (node->type == NODE_CALL) {
        CallNode *c = (CallNode*)node;
        FuncSymbol *fs = find_func_symbol(ctx, c->name);
        if (fs) return fs->ret_type;
        ClassInfo *ci = find_class(ctx, c->name);
        if (ci) { vt.base = TYPE_CLASS; vt.class_name = strdup(c->name); return vt; }
        vt.base = TYPE_INT; return vt;
    }
    else if (node->type == NODE_METHOD_CALL) {
        MethodCallNode *mc = (MethodCallNode*)node;
        // Check for Namespace Method Call (namespace.func())
        if (mc->object->type == NODE_VAR_REF) {
            char *ns_name = ((VarRefNode*)mc->object)->name;
            if (is_namespace(ctx, ns_name)) {
                char mangled[256];
                sprintf(mangled, "%s_%s", ns_name, mc->method_name);
                FuncSymbol *fs = find_func_symbol(ctx, mangled);
                if (fs) return fs->ret_type;
            }
        }
        
        VarType obj_type = codegen_calc_type(ctx, mc->object);
        while (obj_type.ptr_depth > 0) obj_type.ptr_depth--;
        char mangled[256];
        sprintf(mangled, "%s_%s", obj_type.class_name, mc->method_name);
        FuncSymbol *fs = find_func_symbol(ctx, mangled);
        if (fs) return fs->ret_type;
        ClassInfo *ci = find_class(ctx, obj_type.class_name);
        while(ci && ci->parent_name) {
            sprintf(mangled, "%s_%s", ci->parent_name, mc->method_name);
            fs = find_func_symbol(ctx, mangled);
            if (fs) return fs->ret_type;
            ci = find_class(ctx, ci->parent_name);
        }
        return vt;
    }
    else if (node->type == NODE_BINARY_OP) {
        return codegen_calc_type(ctx, ((BinaryOpNode*)node)->left);
    }
    return vt;
}

LLVMValueRef codegen_addr(CodegenCtx *ctx, ASTNode *node) {
    if (node->type == NODE_VAR_REF) {
        VarRefNode *r = (VarRefNode*)node;
        Symbol *sym = find_symbol(ctx, r->name);
        if (sym) return sym->value;
        Symbol *this_sym = find_symbol(ctx, "this");
        if (this_sym) {
            LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder, this_sym->type, this_sym->value, "this_ptr");
            if (this_sym->vtype.class_name) {
                ClassInfo *ci = find_class(ctx, this_sym->vtype.class_name);
                LLVMTypeRef mem_type; VarType mvt;
                int idx = get_member_index(ci, r->name, &mem_type, &mvt);
                if (idx != -1) {
                    LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
                    return LLVMBuildGEP2(ctx->builder, ci->struct_type, this_val, indices, 2, "implicit_mem_addr");
                }
            }
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "Undefined variable '%s'", r->name);
        codegen_error(ctx, node, msg);
        return NULL; // Unreachable
    } 
    else if (node->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)node;
        
        // Namespace Variable Access
        if (ma->object->type == NODE_VAR_REF) {
            char *ns_name = ((VarRefNode*)ma->object)->name;
            if (is_namespace(ctx, ns_name)) {
                char mangled[256];
                sprintf(mangled, "%s_%s", ns_name, ma->member_name);
                Symbol *s = find_symbol(ctx, mangled);
                if (s) return s->value;
            }
        }

        LLVMValueRef obj_addr = NULL;
        VarType obj_type = codegen_calc_type(ctx, ma->object);
        if (ma->object->type == NODE_VAR_REF || ma->object->type == NODE_MEMBER_ACCESS || ma->object->type == NODE_ARRAY_ACCESS) {
             obj_addr = codegen_addr(ctx, ma->object);
             if (obj_type.ptr_depth > 0) {
                 LLVMTypeRef ptr_type = get_llvm_type(ctx, obj_type);
                 obj_addr = LLVMBuildLoad2(ctx->builder, ptr_type, obj_addr, "ptr_obj_load");
             }
        } else {
             codegen_error(ctx, node, "Member access on temporary r-value not supported");
        }
        while(obj_type.ptr_depth > 0) obj_type.ptr_depth--;
        ClassInfo *ci = find_class(ctx, obj_type.class_name);
        if (!ci) codegen_error(ctx, ma->object, "Unknown class type");

        int idx = get_member_index(ci, ma->member_name, NULL, NULL);
        if (idx == -1) { 
             char msg[128];
             snprintf(msg, sizeof(msg), "Unknown member '%s' in class '%s'", ma->member_name, ci->name);
             codegen_error(ctx, node, msg);
        }
        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
        return LLVMBuildGEP2(ctx->builder, ci->struct_type, obj_addr, indices, 2, "mem_gep");
    }
    else if (node->type == NODE_TRAIT_ACCESS) {
        TraitAccessNode *ta = (TraitAccessNode*)node;
        LLVMValueRef obj_addr = codegen_addr(ctx, ta->object); 
        VarType obj_type = codegen_calc_type(ctx, ta->object);
        if (obj_type.ptr_depth > 0) {
             LLVMTypeRef ptr_type = get_llvm_type(ctx, obj_type);
             obj_addr = LLVMBuildLoad2(ctx->builder, ptr_type, obj_addr, "ptr_obj_load");
             while(obj_type.ptr_depth > 0) obj_type.ptr_depth--;
        }
        ClassInfo *ci = find_class(ctx, obj_type.class_name);
        char trait_member[128];
        sprintf(trait_member, "__trait_%s", ta->trait_name);
        int idx = get_member_index(ci, trait_member, NULL, NULL);
        if (idx == -1) { 
            char msg[128];
            snprintf(msg, sizeof(msg), "Class '%s' does not have trait '%s'", ci->name, ta->trait_name);
            codegen_error(ctx, node, msg); 
        }
        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), idx, 0) };
        return LLVMBuildGEP2(ctx->builder, ci->struct_type, obj_addr, indices, 2, "trait_gep");
    }
    else if (node->type == NODE_ARRAY_ACCESS) {
        ArrayAccessNode *an = (ArrayAccessNode*)node;
        LLVMValueRef base_ptr = codegen_addr(ctx, an->target);
        VarType vt = codegen_calc_type(ctx, an->target);
        
        LLVMValueRef idx = codegen_expr(ctx, an->index);
        if (LLVMGetTypeKind(LLVMTypeOf(idx)) != LLVMIntegerTypeKind) {
            idx = LLVMBuildFPToUI(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
        } else {
            idx = LLVMBuildIntCast(ctx->builder, idx, LLVMInt64Type(), "idx_cast");
        }
        
        if (vt.array_size > 0) {
             LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), idx };
             return LLVMBuildGEP2(ctx->builder, get_llvm_type(ctx, vt), base_ptr, indices, 2, "array_elem");
        } else {
             LLVMTypeRef ptr_ptr_type = get_llvm_type(ctx, vt);
             LLVMValueRef arr_base = LLVMBuildLoad2(ctx->builder, ptr_ptr_type, base_ptr, "ptr_base");
             LLVMTypeRef elem_type = LLVMGetElementType(ptr_ptr_type);
             return LLVMBuildGEP2(ctx->builder, elem_type, arr_base, &idx, 1, "ptr_elem");
        }
    } else if (node->type == NODE_UNARY_OP) {
        UnaryOpNode *u = (UnaryOpNode*)node;
        if (u->op == TOKEN_STAR) return codegen_expr(ctx, u->operand);
    }
    codegen_error(ctx, node, "Cannot take address of r-value");
    return NULL;
}

LLVMValueRef codegen_expr(CodegenCtx *ctx, ASTNode *node) {
  if (!node) return LLVMConstInt(LLVMInt32Type(), 0, 0);

  if (node->type == NODE_CALL) {
    CallNode *c = (CallNode*)node;
    ClassInfo *ci = find_class(ctx, c->name);
    if (ci) {
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, ci->struct_type, "ctor_temp");
        ClassMember *m = ci->members;
        ASTNode *arg = c->args;
        while (m) {
            LLVMValueRef mem_ptr = LLVMBuildStructGEP2(ctx->builder, ci->struct_type, alloca, m->index, "mem_ptr");
            LLVMValueRef val_to_store = NULL;
            
            int is_trait = (strncmp(m->name, "__trait_", 8) == 0);

            if (arg && !is_trait) {
                val_to_store = codegen_expr(ctx, arg);
                LLVMTypeRef mem_t = m->type;
                LLVMTypeRef val_t = LLVMTypeOf(val_to_store);
                if (LLVMGetTypeKind(mem_t) == LLVMArrayTypeKind && LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                     LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
                     LLVMValueRef dest = LLVMBuildGEP2(ctx->builder, mem_t, mem_ptr, indices, 2, "dest_ptr");
                     LLVMValueRef args[] = { dest, val_to_store };
                     LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args, 2, "");
                     val_to_store = NULL; 
                } else if (LLVMGetTypeKind(mem_t) == LLVMPointerTypeKind && LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                     if (LLVMGetIntTypeWidth(LLVMGetElementType(mem_t)) == 8) {
                         LLVMValueRef args[] = { val_to_store };
                         val_to_store = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strdup_func), ctx->strdup_func, args, 1, "dup_str");
                     }
                }
                arg = arg->next;
            } else if (m->init_expr) {
                val_to_store = codegen_expr(ctx, m->init_expr);
                LLVMTypeRef mem_t = m->type;
                LLVMTypeRef val_t = LLVMTypeOf(val_to_store);
                if (LLVMGetTypeKind(mem_t) == LLVMArrayTypeKind && LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                     LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
                     LLVMValueRef dest = LLVMBuildGEP2(ctx->builder, mem_t, mem_ptr, indices, 2, "dest_ptr");
                     LLVMValueRef args[] = { dest, val_to_store };
                     LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strcpy_func), ctx->strcpy_func, args, 2, "");
                     val_to_store = NULL; 
                } else if (LLVMGetTypeKind(mem_t) == LLVMPointerTypeKind && LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                     if (LLVMGetIntTypeWidth(LLVMGetElementType(mem_t)) == 8) {
                         LLVMValueRef args[] = { val_to_store };
                         val_to_store = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->strdup_func), ctx->strdup_func, args, 1, "dup_str");
                     }
                }
            } else {
                val_to_store = LLVMConstNull(m->type);
            }
            if (val_to_store) LLVMBuildStore(ctx->builder, val_to_store, mem_ptr);
            m = m->next;
        }
        return LLVMBuildLoad2(ctx->builder, ci->struct_type, alloca, "ctor_res");
    }
    if (strcmp(c->name, "print") == 0) {
      int arg_count = 0; ASTNode *curr = c->args; while(curr) { arg_count++; curr = curr->next; }
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
      curr = c->args; for(int i=0; i<arg_count; i++) { args[i] = codegen_expr(ctx, curr); curr = curr->next; }
      LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, args, arg_count, "");
      free(args); return ret;
    }
    if (strcmp(c->name, "input") == 0) {
       if (c->args) { LLVMValueRef p = codegen_expr(ctx, c->args); LLVMValueRef pa[] = { p }; LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, pa, 1, ""); }
       return LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ctx->input_func), ctx->input_func, NULL, 0, "input_res");
    }
    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, c->name);
    if (!func) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Undefined function '%s'", c->name);
        codegen_error(ctx, node, msg);
    }
    int arg_count = 0; ASTNode *curr = c->args; while(curr) { arg_count++; curr = curr->next; }
    LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
    curr = c->args; for(int i=0; i<arg_count; i++) { args[i] = codegen_expr(ctx, curr); curr = curr->next; }
    LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
    LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count, "");
    free(args); return ret;
  }
  else if (node->type == NODE_VAR_REF) {
      LLVMValueRef addr = codegen_addr(ctx, node);
      VarType vt = codegen_calc_type(ctx, node);
      LLVMTypeRef type = get_llvm_type(ctx, vt);
      if (LLVMGetTypeKind(type) == LLVMArrayTypeKind) {
           LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
           return LLVMBuildGEP2(ctx->builder, type, addr, indices, 2, "array_decay");
      }
      return LLVMBuildLoad2(ctx->builder, type, addr, "var_load");
  }
  else if (node->type == NODE_MEMBER_ACCESS) {
      LLVMValueRef addr = codegen_addr(ctx, node);
      VarType vt = codegen_calc_type(ctx, node);
      LLVMTypeRef type = get_llvm_type(ctx, vt);
      if (LLVMGetTypeKind(type) == LLVMArrayTypeKind) {
           LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
           return LLVMBuildGEP2(ctx->builder, type, addr, indices, 2, "array_decay");
      }
      return LLVMBuildLoad2(ctx->builder, type, addr, "member_load");
  }
  else if (node->type == NODE_ARRAY_ACCESS) {
      LLVMValueRef addr = codegen_addr(ctx, node);
      VarType vt = codegen_calc_type(ctx, node); 
      LLVMTypeRef type = get_llvm_type(ctx, vt); 
      if (LLVMGetTypeKind(type) == LLVMArrayTypeKind) {
           LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
           return LLVMBuildGEP2(ctx->builder, type, addr, indices, 2, "array_elem_decay");
      }
      return LLVMBuildLoad2(ctx->builder, type, addr, "array_load");
  }
  else if (node->type == NODE_ASSIGN) {
      codegen_assign(ctx, (AssignNode*)node); 
      if (((AssignNode*)node)->target) {
          LLVMValueRef addr = codegen_addr(ctx, ((AssignNode*)node)->target);
          VarType vt = codegen_calc_type(ctx, ((AssignNode*)node)->target);
          return LLVMBuildLoad2(ctx->builder, get_llvm_type(ctx, vt), addr, "assign_reload");
      } else {
          Symbol *s = find_symbol(ctx, ((AssignNode*)node)->name);
          if (s) return LLVMBuildLoad2(ctx->builder, s->type, s->value, "assign_reload");
      }
      return LLVMConstInt(LLVMInt32Type(), 0, 0);
  }
  else if (node->type == NODE_INC_DEC) {
    IncDecNode *id = (IncDecNode*)node;
    LLVMValueRef ptr; LLVMTypeRef elem_type;
    if (id->target) { ptr = codegen_addr(ctx, id->target); VarType vt = codegen_calc_type(ctx, id->target); elem_type = get_llvm_type(ctx, vt); } 
    else { Symbol *sym = find_symbol(ctx, id->name); if (!sym) codegen_error(ctx, node, "Undefined variable"); ptr = sym->value; elem_type = sym->type; }
    LLVMValueRef curr = LLVMBuildLoad2(ctx->builder, elem_type, ptr, "curr_val");
    LLVMValueRef next;
    if (LLVMGetTypeKind(elem_type) == LLVMPointerTypeKind) {
        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), (id->op == TOKEN_INCREMENT ? 1 : -1), 1) };
        next = LLVMBuildGEP2(ctx->builder, LLVMGetElementType(elem_type), curr, indices, 1, "ptr_inc");
    } else if (LLVMGetTypeKind(elem_type) == LLVMDoubleTypeKind || LLVMGetTypeKind(elem_type) == LLVMFloatTypeKind) {
        LLVMValueRef one = LLVMConstReal(elem_type, 1.0);
        next = (id->op == TOKEN_INCREMENT) ? LLVMBuildFAdd(ctx->builder, curr, one, "finc") : LLVMBuildFSub(ctx->builder, curr, one, "fdec");
    } else {
        LLVMValueRef one = LLVMConstInt(elem_type, 1, 0);
        next = (id->op == TOKEN_INCREMENT) ? LLVMBuildAdd(ctx->builder, curr, one, "inc") : LLVMBuildSub(ctx->builder, curr, one, "dec");
    }
    LLVMBuildStore(ctx->builder, next, ptr);
    return id->is_prefix ? next : curr;
  }
  else if (node->type == NODE_METHOD_CALL) {
      MethodCallNode *mc = (MethodCallNode*)node;
      
      // Namespace Method Call
      if (mc->object->type == NODE_VAR_REF) {
          char *ns_name = ((VarRefNode*)mc->object)->name;
          if (is_namespace(ctx, ns_name)) {
             char mangled[256];
             sprintf(mangled, "%s_%s", ns_name, mc->method_name);
             LLVMValueRef func = LLVMGetNamedFunction(ctx->module, mangled);
             if (!func) {
                 char msg[128];
                 snprintf(msg, sizeof(msg), "Namespace method '%s' not found", mangled);
                 codegen_error(ctx, node, msg);
             }
             int arg_count = 0; ASTNode *arg = mc->args; while(arg) { arg_count++; arg = arg->next; }
             LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
             arg = mc->args; int i = 0;
             while(arg) { args[i++] = codegen_expr(ctx, arg); arg = arg->next; }
             LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
             LLVMValueRef ret = LLVMBuildCall2(ctx->builder, ftype, func, args, arg_count, "");
             free(args); return ret;
          }
      }

      LLVMValueRef obj_ptr = codegen_addr(ctx, mc->object);
      VarType obj_type = codegen_calc_type(ctx, mc->object);
      if (obj_type.ptr_depth > 0) {
          LLVMTypeRef ptr_type = get_llvm_type(ctx, obj_type);
          obj_ptr = LLVMBuildLoad2(ctx->builder, ptr_type, obj_ptr, "this_ptr_deref");
          while(obj_type.ptr_depth > 0) obj_type.ptr_depth--;
      }
      char mangled[256];
      sprintf(mangled, "%s_%s", obj_type.class_name, mc->method_name);
      LLVMValueRef func = LLVMGetNamedFunction(ctx->module, mangled);
      ClassInfo *ci = find_class(ctx, obj_type.class_name);
      if (!func) {
          while (ci && ci->parent_name) {
              sprintf(mangled, "%s_%s", ci->parent_name, mc->method_name);
              func = LLVMGetNamedFunction(ctx->module, mangled);
              if (func) {
                  ClassInfo *pi = find_class(ctx, ci->parent_name);
                  obj_ptr = LLVMBuildPointerCast(ctx->builder, obj_ptr, LLVMPointerType(pi->struct_type, 0), "cast_parent");
                  break; 
              }
              ci = find_class(ctx, ci->parent_name);
          }
      }
      if (!func) { 
          char msg[128];
          snprintf(msg, sizeof(msg), "Method '%s' not found", mc->method_name);
          codegen_error(ctx, node, msg); 
      }
      int arg_count = 1; ASTNode *arg = mc->args; while(arg) { arg_count++; arg = arg->next; }
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * arg_count);
      args[0] = obj_ptr; 
      arg = mc->args; int i = 1;
      while(arg) { args[i++] = codegen_expr(ctx, arg); arg = arg->next; }
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
      char *fmt = format_string(l->val.str_val);
      LLVMValueRef gstr = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "str_lit");
      free(fmt);
      return gstr;
    }
    return LLVMConstInt(get_llvm_type(ctx, l->var_type), l->val.int_val, 0);
  }
  else if (node->type == NODE_TYPEOF) {
      UnaryOpNode *u = (UnaryOpNode*)node;
      VarType vt = codegen_calc_type(ctx, u->operand);
      char type_name[64] = "unknown";
      if (vt.base == TYPE_INT) strcpy(type_name, "int");
      else if (vt.base == TYPE_FLOAT) strcpy(type_name, "single");
      else if (vt.base == TYPE_DOUBLE) strcpy(type_name, "double");
      else if (vt.base == TYPE_STRING) strcpy(type_name, "string");
      else if (vt.base == TYPE_BOOL) strcpy(type_name, "bool");
      else if (vt.base == TYPE_CHAR) strcpy(type_name, "char");
      else if (vt.base == TYPE_CLASS) strcpy(type_name, vt.class_name ? vt.class_name : "class");
      else if (vt.base == TYPE_VOID) strcpy(type_name, "void");
      LLVMValueRef gstr = LLVMBuildGlobalStringPtr(ctx->builder, type_name, "typeof_str");
      return gstr;
  }
  else if (node->type == NODE_BINARY_OP) {
      BinaryOpNode *op = (BinaryOpNode*)node;
      LLVMValueRef l = codegen_expr(ctx, op->left);
      LLVMValueRef r = codegen_expr(ctx, op->right);
      if (op->op == TOKEN_PLUS) return LLVMBuildAdd(ctx->builder, l, r, "add");
      if (op->op == TOKEN_MINUS) return LLVMBuildSub(ctx->builder, l, r, "sub");
      if (op->op == TOKEN_STAR) return LLVMBuildMul(ctx->builder, l, r, "mul");
      if (op->op == TOKEN_SLASH) return LLVMBuildSDiv(ctx->builder, l, r, "div");
      return LLVMConstInt(LLVMInt32Type(), 0, 0);
  }
  return LLVMConstInt(LLVMInt32Type(), 0, 0);
}
