
#ifndef LLVM_C_TRANSFORMS_PASSBUILDER_H
#define LLVM_C_TRANSFORMS_PASSBUILDER_H
#include "llvm-c/Error.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Types.h"
#include "llvm-c/Visibility.h"

LLVM_C_EXTERN_C_BEGIN

typedef struct LLVMOpaquePassBuilderOptions *LLVMPassBuilderOptionsRef;

LLVM_C_ABI LLVMErrorRef LLVMRunPasses(LLVMModuleRef M, const char *Passes,
                                      LLVMTargetMachineRef TM,
                                      LLVMPassBuilderOptionsRef Options);

LLVM_C_ABI LLVMErrorRef LLVMRunPassesOnFunction(
    LLVMValueRef F, const char *Passes, LLVMTargetMachineRef TM,
    LLVMPassBuilderOptionsRef Options);

LLVM_C_ABI LLVMPassBuilderOptionsRef LLVMCreatePassBuilderOptions(void);

LLVM_C_ABI void
LLVMPassBuilderOptionsSetVerifyEach(LLVMPassBuilderOptionsRef Options,
                                    LLVMBool VerifyEach);

LLVM_C_ABI void
LLVMPassBuilderOptionsSetDebugLogging(LLVMPassBuilderOptionsRef Options,
                                      LLVMBool DebugLogging);

LLVM_C_ABI void
LLVMPassBuilderOptionsSetAAPipeline(LLVMPassBuilderOptionsRef Options,
                                    const char *AAPipeline);
LLVM_C_ABI void
LLVMPassBuilderOptionsSetLoopInterleaving(LLVMPassBuilderOptionsRef Options,
                                          LLVMBool LoopInterleaving);
LLVM_C_ABI void
LLVMPassBuilderOptionsSetLoopVectorization(LLVMPassBuilderOptionsRef Options,
                                           LLVMBool LoopVectorization);
LLVM_C_ABI void
LLVMPassBuilderOptionsSetSLPVectorization(LLVMPassBuilderOptionsRef Options,
                                          LLVMBool SLPVectorization);
LLVM_C_ABI void
LLVMPassBuilderOptionsSetLoopUnrolling(LLVMPassBuilderOptionsRef Options,
                                       LLVMBool LoopUnrolling);
LLVM_C_ABI void LLVMPassBuilderOptionsSetForgetAllSCEVInLoopUnroll(
    LLVMPassBuilderOptionsRef Options, LLVMBool ForgetAllSCEVInLoopUnroll);
LLVM_C_ABI void
LLVMPassBuilderOptionsSetLicmMssaOptCap(LLVMPassBuilderOptionsRef Options,
                                        unsigned LicmMssaOptCap);
LLVM_C_ABI void LLVMPassBuilderOptionsSetLicmMssaNoAccForPromotionCap(
    LLVMPassBuilderOptionsRef Options, unsigned LicmMssaNoAccForPromotionCap);
LLVM_C_ABI void
LLVMPassBuilderOptionsSetCallGraphProfile(LLVMPassBuilderOptionsRef Options,
                                          LLVMBool CallGraphProfile);
LLVM_C_ABI void
LLVMPassBuilderOptionsSetMergeFunctions(LLVMPassBuilderOptionsRef Options,
                                        LLVMBool MergeFunctions);
LLVM_C_ABI void
LLVMPassBuilderOptionsSetInlinerThreshold(LLVMPassBuilderOptionsRef Options,
                                          int Threshold);

LLVM_C_ABI void
LLVMDisposePassBuilderOptions(LLVMPassBuilderOptionsRef Options);

LLVM_C_EXTERN_C_END
#endif // LLVM_C_TRANSFORMS_PASSBUILDER_H
