#include "cli.h"
#include "parser.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>

#if defined(HAVE_READLINE)
  #include <readline/readline.h>
  #include <readline/history.h>
#endif

// Buffer for reading input
#define INPUT_BUFFER_SIZE 4096

// --- INPUT HANDLING WITH BRACE COUNTING ---
char* get_smart_input(const char* prompt) {
    char *input_buffer = malloc(INPUT_BUFFER_SIZE);
    if (!input_buffer) return NULL;
    input_buffer[0] = '\0';
    
    int total_len = 0;
    int brace_depth = 0;
    
    char *line;
    int first_line = 1;

    while (1) {
        #if defined(HAVE_READLINE)
            line = readline(first_line ? prompt : "... ");
            if (!line) { // EOF
                free(input_buffer);
                return NULL; 
            }
            if (first_line && strlen(line) > 0) add_history(line);
        #else
            printf("%s", first_line ? prompt : "... ");
            char raw_buf[1024];
            if (!fgets(raw_buf, sizeof(raw_buf), stdin)) {
                free(input_buffer);
                return NULL;
            }
            // Remove newline
            raw_buf[strcspn(raw_buf, "\n")] = 0;
            line = strdup(raw_buf);
        #endif
        
        // Append line to buffer
        int line_len = strlen(line);
        if (total_len + line_len + 2 >= INPUT_BUFFER_SIZE) {
            printf("Input too long!\n");
            free(line);
            free(input_buffer);
            return NULL;
        }
        
        strcat(input_buffer, line);
        strcat(input_buffer, " "); // Add space instead of newline for parser continuity? or \n?
        // Parser ignores whitespace, so space is fine. Newline is better for line tracking.
        // Actually, parser eats newlines as whitespace.
        
        // Count Braces
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
    printf("==========================================\n");
    printf("       Alkyl Command Line Interface       \n");
    printf("==========================================\n");
    printf("Type 'exit' or 'quit' to leave.\n\n");

    // 1. Initialize LLVM JIT
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();
    LLVMLinkInMCJIT();

    // 2. Create Module and Engine
    LLVMModuleRef module = LLVMModuleCreateWithName("alkyl_jit_session");
    LLVMExecutionEngineRef engine;
    char *error = NULL;

    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr, "Failed to create execution engine: %s\n", error);
        LLVMDisposeMessage(error);
        return 1;
    }

    LLVMBuilderRef builder = LLVMCreateBuilder();
    CodegenCtx ctx;
    codegen_init_ctx(&ctx, module, builder);

    int cmd_count = 0;
    jmp_buf recovery_env;

    while (1) {
        char *buffer = get_smart_input("alkyl> ");
        if (!buffer) break; // EOF

        if (strcmp(buffer, "exit ") == 0 || strcmp(buffer, "quit ") == 0) { // Space added by loop
            free(buffer);
            break;
        }

        // Add semicolon if missing (REPL convenience), but check if it ends with }
        // trim trailing space
        int len = strlen(buffer);
        while(len > 0 && buffer[len-1] == ' ') len--;
        buffer[len] = '\0';
        
        if (len > 0 && buffer[len-1] != ';' && buffer[len-1] != '}') {
            strcat(buffer, ";");
        }

        // --- PARSING WITH RECOVERY ---
        if (setjmp(recovery_env) == 0) {
            parser_set_recovery(&recovery_env);
            
            Lexer l;
            lexer_init(&l, buffer);
            
            ASTNode *root = parse_program(&l);
            if (!root) { free(buffer); continue; }

            ASTNode *curr = root;
            while (curr) {
                if (curr->type == NODE_FUNC_DEF) {
                    codegen_func_def(&ctx, (FuncDefNode*)curr);
                    printf("Function '%s' defined.\n", ((FuncDefNode*)curr)->name);
                } 
                else if (curr->type == NODE_VAR_DECL) {
                    VarDeclNode *vd = (VarDeclNode*)curr;
                    LLVMTypeRef type = (vd->var_type == VAR_AUTO) ? LLVMInt32Type() : get_llvm_type(vd->var_type);
                    LLVMValueRef gVar = LLVMAddGlobal(module, type, vd->name);
                    LLVMSetLinkage(gVar, LLVMCommonLinkage);
                    LLVMSetInitializer(gVar, LLVMConstNull(type));
                    
                    add_symbol(&ctx, vd->name, gVar, type, vd->is_array, vd->is_mutable);

                    if (vd->initializer) {
                        char temp_name[64];
                        sprintf(temp_name, "__init_%s_%d", vd->name, cmd_count++);
                        LLVMTypeRef void_type = LLVMFunctionType(LLVMVoidType(), NULL, 0, false);
                        LLVMValueRef init_func = LLVMAddFunction(module, temp_name, void_type);
                        LLVMBasicBlockRef entry = LLVMAppendBasicBlock(init_func, "entry");
                        LLVMPositionBuilderAtEnd(builder, entry);
                        
                        LLVMValueRef init_val = codegen_expr(&ctx, vd->initializer);
                        LLVMBuildStore(builder, init_val, gVar);
                        LLVMBuildRetVoid(builder);

                        LLVMGenericValueRef exec_res = LLVMRunFunction(engine, init_func, 0, NULL);
                        LLVMDisposeGenericValue(exec_res);
                        LLVMDeleteFunction(init_func);
                    }
                    printf("%s defined.\n", vd->name);
                }
                else {
                    // Expression or Statement wrapper
                    char temp_name[64];
                    sprintf(temp_name, "__anon_%d", cmd_count++);
                    
                    LLVMTypeRef func_type = LLVMFunctionType(LLVMInt32Type(), NULL, 0, false);
                    LLVMValueRef func = LLVMAddFunction(module, temp_name, func_type);
                    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
                    LLVMPositionBuilderAtEnd(builder, entry);

                    LLVMValueRef result = NULL;
                    
                    if (curr->type != NODE_ASSIGN && curr->type != NODE_LOOP && curr->type != NODE_IF && curr->type != NODE_RETURN) {
                        result = codegen_expr(&ctx, curr);
                        // Simple cast logic for printing
                        LLVMTypeRef rtype = LLVMTypeOf(result);
                        if (LLVMGetTypeKind(rtype) == LLVMDoubleTypeKind || LLVMGetTypeKind(rtype) == LLVMFloatTypeKind)
                            result = LLVMBuildFPToUI(builder, result, LLVMInt32Type(), "cast");
                        else if (LLVMGetTypeKind(rtype) != LLVMIntegerTypeKind)
                            result = LLVMConstInt(LLVMInt32Type(), 0, 0); 
                    } else {
                        codegen_node(&ctx, curr);
                        result = LLVMConstInt(LLVMInt32Type(), 0, 0);
                    }

                    LLVMBuildRet(builder, result);

                    LLVMGenericValueRef exec_res = LLVMRunFunction(engine, func, 0, NULL);
                    int int_res = (int)LLVMGenericValueToInt(exec_res, 0);
                    
                    if (curr->type != NODE_ASSIGN && curr->type != NODE_LOOP && curr->type != NODE_IF && curr->type != NODE_RETURN) {
                        printf("%d\n", int_res);
                    }
                    
                    LLVMDisposeGenericValue(exec_res);
                    LLVMDeleteFunction(func);
                }
                curr = curr->next;
            }
            free_ast(root);

        } else {
            // Error handling block (longjmp landed here)
            // Parser error message already printed by parser_fail
            parser_set_recovery(NULL); // Reset
        }
        
        free(buffer);
    }

    LLVMDisposeBuilder(builder);
    LLVMDisposeExecutionEngine(engine);
    return 0;
}

int main(int argc, char **argv) {
    return run_repl();
}
