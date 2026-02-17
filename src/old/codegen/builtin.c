#include "codegen.h"

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

// gen node
LLVMValueRef gen_typeof(CodegenCtx *ctx, UnaryOpNode *tn) {
      VarType t = codegen_calc_type(ctx, tn->operand);
      char buf[256];
      get_type_name(t, buf);
      return LLVMBuildGlobalStringPtr(ctx->builder, buf, "typeof_str");
}

LLVMValueRef gen_reflection(CodegenCtx *ctx, UnaryOpNode *u, int is_method) {
      VarType t = codegen_calc_type(ctx, u->operand);
      if (t.class_name) {
          ClassInfo *ci = find_class(ctx, t.class_name);
          if (!ci) codegen_error(ctx, (ASTNode*)u, "Unknown class for reflection");
          
          if (is_method) {
               // hasmethod logic
              int count = ci->method_count;
              LLVMTypeRef str_type = LLVMPointerType(LLVMInt8Type(), 0);
              LLVMTypeRef arr_type = LLVMArrayType(str_type, count + 1); 
              
              LLVMValueRef *vals = malloc(sizeof(LLVMValueRef) * (count + 1));
              for(int i=0; i<count; i++) {
                  vals[i] = LLVMBuildGlobalStringPtr(ctx->builder, ci->method_names[i], "method_name");
              }
              vals[count] = LLVMConstPointerNull(str_type);

              LLVMValueRef const_arr = LLVMConstArray(str_type, vals, count + 1);
              LLVMValueRef global_arr = LLVMAddGlobal(ctx->module, arr_type, "method_list");
              LLVMSetInitializer(global_arr, const_arr);
              LLVMSetGlobalConstant(global_arr, 1);
              LLVMSetLinkage(global_arr, LLVMPrivateLinkage);
              
              free(vals);
              
              LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
              return LLVMBuildGEP2(ctx->builder, arr_type, global_arr, indices, 2, "method_list_ptr");
          } else {
               // hasattribute logic
              int count = 0;
              ClassMember *m = ci->members;
              while(m) { count++; m = m->next; }
              
              LLVMTypeRef str_type = LLVMPointerType(LLVMInt8Type(), 0);
              LLVMTypeRef arr_type = LLVMArrayType(str_type, count + 1);
              
              LLVMValueRef *vals = malloc(sizeof(LLVMValueRef) * (count + 1));
              m = ci->members;
              int i = 0;
              while(m) {
                  vals[i++] = LLVMBuildGlobalStringPtr(ctx->builder, m->name, "attr_name");
                  m = m->next;
              }
              vals[count] = LLVMConstPointerNull(str_type);

              LLVMValueRef const_arr = LLVMConstArray(str_type, vals, count + 1);
              LLVMValueRef global_arr = LLVMAddGlobal(ctx->module, arr_type, "attr_list");
              LLVMSetInitializer(global_arr, const_arr);
              LLVMSetGlobalConstant(global_arr, 1);
              LLVMSetLinkage(global_arr, LLVMPrivateLinkage);
              
              free(vals);
              
              LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
              return LLVMBuildGEP2(ctx->builder, arr_type, global_arr, indices, 2, "attr_list_ptr");
          }
      }
      return LLVMConstPointerNull(LLVMPointerType(LLVMPointerType(LLVMInt8Type(), 0), 0));
}

