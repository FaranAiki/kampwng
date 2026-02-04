#include "cli.h"
#include "parser.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <setjmp.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <readline/readline.h>
#include <readline/history.h>

#define COL_RESET   "\033[0m"
#define COL_GREEN   "\033[1;32m"
#define COL_BLUE    "\033[1;34m"
#define COL_RED     "\033[1;31m"
#define COL_CYAN    "\033[1;36m"
#define COL_YELLOW  "\033[1;33m"
#define INPUT_BUFFER_SIZE 4096

char* get_smart_input(const char* prompt) {
    char *input_buffer = malloc(INPUT_BUFFER_SIZE);
    if (!input_buffer) return NULL;
    input_buffer[0] = '\0';
    int total_len = 0;
    int brace_depth = 0;
    char *line;
    int first_line = 1;
    while (1) {
        line = readline(first_line ? prompt : "... ");
        if (!line) { free(input_buffer); return NULL; }
        if (first_line && strlen(line) > 0) add_history(line);
        int line_len = strlen(line);
        if (total_len + line_len + 2 >= INPUT_BUFFER_SIZE) {
            printf(COL_RED "Input too long!\n" COL_RESET);
            free(line); free(input_buffer); return NULL;
        }
        strcat(input_buffer, line);
        strcat(input_buffer, " "); 
        total_len += line_len + 1;
        int in_string = 0;
        int in_char = 0;
        for (int i = 0; i < line_len; i++) {
            if (line[i] == '"' && !in_char) in_string = !in_string;
            if (line[i] == '\'' && !in_string) in_char = !in_char;
            if (!in_string && !in_char) {
                if (line[i] == '{') brace_depth++;
                if (line[i] == '}') brace_depth--;
            }
        }
        free(line);
        if (brace_depth <= 0) break;
        first_line = 0;
    }
    return input_buffer;
}

int run_repl(void) {
    printf(COL_CYAN "==========================================\n");
    printf("       Alkyl Command Line Interface       \n");
    printf("==========================================\n" COL_RESET);
    printf("Type " COL_YELLOW "'exit'" COL_RESET " or " COL_YELLOW "'quit'" COL_RESET " to leave.\n\n");

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();
    LLVMLinkInMCJIT();

    LLVMModuleRef module = LLVMModuleCreateWithName("alkyl_jit_session");
    LLVMExecutionEngineRef engine;
    char *error = NULL;

    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr, COL_RED "Failed to create execution engine: %s\n" COL_RESET, error);
        LLVMDisposeMessage(error);
        return 1;
    }

    LLVMBuilderRef builder = LLVMCreateBuilder();
    CodegenCtx ctx;
    codegen_init_ctx(&ctx, module, builder);
    LLVMDisposeBuilder(builder);

    int cmd_count = 0;
    jmp_buf recovery_env;

    while (1) {
        parser_reset();

        char *buffer = get_smart_input(COL_GREEN "alkyl> " COL_RESET);
        if (!buffer) break; 

        if (strcmp(buffer, "exit ") == 0 || strcmp(buffer, "quit ") == 0) { 
            free(buffer);
            break;
        }

        int len = strlen(buffer);
        while(len > 0 && buffer[len-1] == ' ') len--;
        buffer[len] = '\0';
        
        if (len > 0 && buffer[len-1] != ';' && buffer[len-1] != '}') {
            if (len + 1 < INPUT_BUFFER_SIZE) {
                strcat(buffer, ";");
            }
        }

        if (setjmp(recovery_env) == 0) {
            parser_set_recovery(&recovery_env);
            
            Lexer l;
            lexer_init(&l, buffer);
            
            ASTNode *root = parse_program(&l);
            if (!root) { free(buffer); continue; }

            LLVMModuleRef out_mod;
            char *remove_err = NULL;
            if (LLVMRemoveModule(engine, module, &out_mod, &remove_err) != 0) {
                // Ignore first run error
            }
            
            LLVMBuilderRef loop_builder = LLVMCreateBuilder();
            ctx.builder = loop_builder;

            ASTNode *curr = root;
            while (curr) {
                if (curr->type == NODE_FUNC_DEF) {
                    codegen_func_def(&ctx, (FuncDefNode*)curr);
                    printf(COL_BLUE "Function '%s' defined.\n" COL_RESET, ((FuncDefNode*)curr)->name);
                } 
                else if (curr->type == NODE_VAR_DECL) {
                    VarDeclNode *vd = (VarDeclNode*)curr;
                    LLVMTypeRef type = (vd->var_type.base == TYPE_AUTO) ? LLVMInt32Type() : get_llvm_type(vd->var_type);
                    LLVMValueRef gVar = LLVMAddGlobal(module, type, vd->name);
                    LLVMSetLinkage(gVar, LLVMCommonLinkage);
                    LLVMSetInitializer(gVar, LLVMConstNull(type));
                    
                    add_symbol(&ctx, vd->name, gVar, type, vd->var_type, vd->is_array, vd->is_mutable);

                    if (vd->initializer) {
                        char temp_name[64];
                        sprintf(temp_name, "__init_%s_%d", vd->name, cmd_count++);
                        LLVMTypeRef void_type = LLVMFunctionType(LLVMVoidType(), NULL, 0, false);
                        LLVMValueRef init_func = LLVMAddFunction(module, temp_name, void_type);
                        LLVMBasicBlockRef entry = LLVMAppendBasicBlock(init_func, "entry");
                        LLVMPositionBuilderAtEnd(loop_builder, entry);
                        
                        LLVMValueRef init_val = codegen_expr(&ctx, vd->initializer);
                        LLVMBuildStore(loop_builder, init_val, gVar);
                        LLVMBuildRetVoid(loop_builder);
                        
                        LLVMAddModule(engine, module);
                        LLVMGenericValueRef exec_res = LLVMRunFunction(engine, init_func, 0, NULL);
                        LLVMDisposeGenericValue(exec_res);
                        LLVMRemoveModule(engine, module, &out_mod, &remove_err); 
                    }
                    printf(COL_BLUE "%s defined.\n" COL_RESET, vd->name);
                }
                else {
                    char temp_name[64];
                    sprintf(temp_name, "__anon_%d", cmd_count++);
                    
                    LLVMTypeRef func_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
                    LLVMValueRef func = LLVMAddFunction(module, temp_name, func_type);
                    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
                    LLVMPositionBuilderAtEnd(loop_builder, entry);

                    LLVMValueRef result = NULL;
                    
                    if (curr->type != NODE_ASSIGN && curr->type != NODE_LOOP && curr->type != NODE_IF && curr->type != NODE_RETURN) {
                        result = codegen_expr(&ctx, curr);
                        LLVMTypeRef rtype = LLVMTypeOf(result);
                        if (LLVMGetTypeKind(rtype) == LLVMDoubleTypeKind || LLVMGetTypeKind(rtype) == LLVMFloatTypeKind)
                            result = LLVMBuildFPToUI(loop_builder, result, LLVMInt32Type(), "cast");
                        else if (LLVMGetTypeKind(rtype) != LLVMIntegerTypeKind)
                            result = LLVMConstInt(LLVMInt32Type(), 0, 0); 
                    } else {
                        ASTNode *next_temp = curr->next;
                        curr->next = NULL; 
                        codegen_node(&ctx, curr);
                        curr->next = next_temp; 
                        result = LLVMConstInt(LLVMInt32Type(), 0, 0);
                    }

                    LLVMBuildRet(loop_builder, result);
                    LLVMAddModule(engine, module);
                    LLVMGenericValueRef exec_res = LLVMRunFunction(engine, func, 0, NULL);
                    int int_res = (int)LLVMGenericValueToInt(exec_res, 0);
                    
                    if (curr->type != NODE_ASSIGN && curr->type != NODE_LOOP && curr->type != NODE_IF && curr->type != NODE_RETURN) {
                        printf("%d\n", int_res);
                    }
                    LLVMDisposeGenericValue(exec_res);
                    LLVMRemoveModule(engine, module, &out_mod, &remove_err);
                }
                curr = curr->next;
            }
            LLVMAddModule(engine, module);
            LLVMDisposeBuilder(loop_builder);
            free_ast(root);
        } else {
            parser_set_recovery(NULL); 
        }
        free(buffer);
    }
    LLVMDisposeExecutionEngine(engine);
    return 0;
}

int main() {
  return run_repl();
}
