/*
 * Someone please teach me what the hell 
 * is LLVM
 *
 */

#include "kampwng.h"
#include <stdio.h>
#include <stdlib.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>

char* read_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file.kmpg>\n", argv[0]);
        return 1;
    }

    char *code = read_file(argv[1]);
    if (!code) { fprintf(stderr, "Could not read file\n"); return 1; }

    Lexer l;
    lexer_init(&l, code);
    ASTNode *root = parse_program(&l);

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    LLVMModuleRef module = codegen_generate(root, "kampwng_mod");

    char *error = NULL;
    if (LLVMVerifyModule(module, LLVMAbortProcessAction, &error)) {
        fprintf(stderr, "LLVM Verification Error: %s\n", error);
        LLVMDisposeMessage(error);
        return 1;
    }


    // TODO add more target
    char *triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char *err_msg = NULL;
    LLVMGetTargetFromTriple(triple, &target, &err_msg);

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple, "generic", "", 
        LLVMCodeGenLevelAggressive, LLVMRelocPIC, LLVMCodeModelDefault
    );

    if (LLVMTargetMachineEmitToFile(machine, module, "out.o", LLVMObjectFile, &err_msg) != 0) {
        fprintf(stderr, "Emit Error: %s\n", err_msg);
        return 1;
    }

    printf("Compiled to out.o\n");
    
    // TODO add more options
    system("gcc out.o -o out -no-pie");
    printf("Linked to ./out\n");

    // Cleanup (Simplified for tutorial)
    LLVMDisposeModule(module);
    free(code);
    
    return 0;
}
