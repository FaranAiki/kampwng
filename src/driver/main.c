#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>

#define BASENAME "out"

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
    printf("Usage: %s <file.aky> [-l<lib>]\n", argv[0]);
    return 1;
  }

  char *filename = NULL;
  char link_flags[1024] = {0};

  for (int i = 1; i < argc; i++) {
      if (strncmp(argv[i], "-l", 2) == 0) {
          if (strlen(link_flags) + strlen(argv[i]) + 2 < sizeof(link_flags)) {
              strcat(link_flags, " ");
              strcat(link_flags, argv[i]);
          } else {
              fprintf(stderr, "Too many link flags\n");
              return 1;
          }
      } else {
          filename = argv[i];
      }
  }

  if (!filename) {
      fprintf(stderr, "No input file specified\n");
      return 1;
  }

  char *code = read_file(filename);
  if (!code) { fprintf(stderr, "Could not read file: %s\n", filename); return 1; }

  Lexer l;
  lexer_init(&l, code);
  l.filename = filename; // Set filename for diagnostic context

  debug_step("Finished lexing. Start parsing.");

  // generate for debugging 
  Lexer l_debug = l;
  lexer_init(&l_debug, code);
  l.filename = filename;

  to_token_out(&l_debug, BASENAME ".tok");

  ASTNode *root = parse_program(&l);
  
  if (!root && l.parser_error_count > 0) {
      free(code);
      return 1;
  }
  
  // ASTNode *root_debug = parse_program(&l_debug);

  to_ast_out(root, BASENAME ".ast");

  debug_step("Finished semantic analysis. Start macro-linking.");
  ASTNode *curr = root;
  while(curr) {
    if (curr->type == NODE_LINK) {
      LinkNode *lnk = (LinkNode*)curr;
      if (strlen(link_flags) + strlen(lnk->lib_name) + 4 < sizeof(link_flags)) {
        strcat(link_flags, " -l");
        strcat(link_flags, lnk->lib_name);
      }
    }
    curr = curr->next;
  }

  debug_step("Finished macro-linking. Start Codegen.");

  // Pass to ALIR 
  AlirModule *alir_module = alir_generate(root); 
  alir_emit_to_file(alir_module, BASENAME ".alir");
  // TODO: THIS NEEDS A FUCKING REFORMAT NOOOOOOOOOOOOOOOOOOOOO 

  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();
  LLVMInitializeNativeAsmParser();

  // Pass source code to codegen for error reporting
  LLVMModuleRef module = codegen_generate(root, "alkyl_llvm", code);

  char *error = NULL;
  if (LLVMVerifyModule(module, LLVMAbortProcessAction, &error)) {
    fprintf(stderr, "LLVM Verification Error: %s\n", error);
    LLVMDisposeMessage(error);
    return 1;
  }

  char *triple = LLVMGetDefaultTargetTriple();
  LLVMTargetRef target;
  char *err_msg = NULL;
  LLVMGetTargetFromTriple(triple, &target, &err_msg);

  LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
    target, triple, "generic", "", 
    LLVMCodeGenLevelAggressive, LLVMRelocPIC, LLVMCodeModelDefault
  );

  if (LLVMTargetMachineEmitToFile(machine, module, BASENAME ".o", LLVMObjectFile, &err_msg) != 0) {
    fprintf(stderr, "Emit Error: %s\n", err_msg);
    return 1;
  }

  if (LLVMPrintModuleToFile(module, BASENAME ".ll", &err_msg) != 0) {
    fprintf(stderr, "Emit Error: %s\n", err_msg);
    return 1;
  }

  printf("Compiled to "BASENAME".o\n");
  
  char cmd[2048];
  snprintf(cmd, sizeof(cmd), "gcc -g -O0 "BASENAME".o -o "BASENAME" -no-pie %s", link_flags);
  
  printf("Linking: %s\n", cmd);
  int res = system(cmd);
  if (res == 0) {
    printf("Linked to ./"BASENAME"\n");
  } else {
    printf("Linking failed.\n");
  }

  LLVMDisposeModule(module);
  free(code);
  free_ast(root);
  
  return 0;
}
