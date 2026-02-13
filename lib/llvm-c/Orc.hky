#ifndef LLVM_C_ORC_H
#define LLVM_C_ORC_H
#include "llvm-c/Error.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Types.h"
#include "llvm-c/Visibility.h"
LLVM_C_EXTERN_C_BEGIN
typedef uint64_t LLVMOrcJITTargetAddress;
typedef uint64_t LLVMOrcExecutorAddress;
typedef enum {
  LLVMJITSymbolGenericFlagsNone = 0,
  LLVMJITSymbolGenericFlagsExported = 1U << 0,
  LLVMJITSymbolGenericFlagsWeak = 1U << 1,
  LLVMJITSymbolGenericFlagsCallable = 1U << 2,
  LLVMJITSymbolGenericFlagsMaterializationSideEffectsOnly = 1U << 3
} LLVMJITSymbolGenericFlags;
typedef uint8_t LLVMJITSymbolTargetFlags;
typedef struct {
  uint8_t GenericFlags;
  uint8_t TargetFlags;
} LLVMJITSymbolFlags;
typedef struct {
  LLVMOrcExecutorAddress Address;
  LLVMJITSymbolFlags Flags;
} LLVMJITEvaluatedSymbol;
typedef struct LLVMOrcOpaqueExecutionSession *LLVMOrcExecutionSessionRef;
typedef void (*LLVMOrcErrorReporterFunction)(void *Ctx, LLVMErrorRef Err);
typedef struct LLVMOrcOpaqueSymbolStringPool *LLVMOrcSymbolStringPoolRef;
typedef struct LLVMOrcOpaqueSymbolStringPoolEntry
    *LLVMOrcSymbolStringPoolEntryRef;
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMJITSymbolFlags Flags;
} LLVMOrcCSymbolFlagsMapPair;
typedef LLVMOrcCSymbolFlagsMapPair *LLVMOrcCSymbolFlagsMapPairs;
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMJITEvaluatedSymbol Sym;
} LLVMOrcCSymbolMapPair;
typedef LLVMOrcCSymbolMapPair *LLVMOrcCSymbolMapPairs;
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMJITSymbolFlags Flags;
} LLVMOrcCSymbolAliasMapEntry;
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMOrcCSymbolAliasMapEntry Entry;
} LLVMOrcCSymbolAliasMapPair;
typedef LLVMOrcCSymbolAliasMapPair *LLVMOrcCSymbolAliasMapPairs;
typedef struct LLVMOrcOpaqueJITDylib *LLVMOrcJITDylibRef;
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef *Symbols;
  size_t Length;
} LLVMOrcCSymbolsList;
typedef struct {
  LLVMOrcJITDylibRef JD;
  LLVMOrcCSymbolsList Names;
} LLVMOrcCDependenceMapPair;
typedef LLVMOrcCDependenceMapPair *LLVMOrcCDependenceMapPairs;
typedef struct {
  LLVMOrcCSymbolsList Symbols;
  LLVMOrcCDependenceMapPairs Dependencies;
  size_t NumDependencies;
} LLVMOrcCSymbolDependenceGroup;
typedef enum {
  LLVMOrcLookupKindStatic,
  LLVMOrcLookupKindDLSym
} LLVMOrcLookupKind;
typedef enum {
  LLVMOrcJITDylibLookupFlagsMatchExportedSymbolsOnly,
  LLVMOrcJITDylibLookupFlagsMatchAllSymbols
} LLVMOrcJITDylibLookupFlags;
typedef struct {
  LLVMOrcJITDylibRef JD;
  LLVMOrcJITDylibLookupFlags JDLookupFlags;
} LLVMOrcCJITDylibSearchOrderElement;
typedef LLVMOrcCJITDylibSearchOrderElement *LLVMOrcCJITDylibSearchOrder;
typedef enum {
  LLVMOrcSymbolLookupFlagsRequiredSymbol,
  LLVMOrcSymbolLookupFlagsWeaklyReferencedSymbol
} LLVMOrcSymbolLookupFlags;
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMOrcSymbolLookupFlags LookupFlags;
} LLVMOrcCLookupSetElement;
typedef LLVMOrcCLookupSetElement *LLVMOrcCLookupSet;
typedef struct LLVMOrcOpaqueMaterializationUnit *LLVMOrcMaterializationUnitRef;
typedef struct LLVMOrcOpaqueMaterializationResponsibility
    *LLVMOrcMaterializationResponsibilityRef;
typedef void (*LLVMOrcMaterializationUnitMaterializeFunction)(
    void *Ctx, LLVMOrcMaterializationResponsibilityRef MR);
typedef void (*LLVMOrcMaterializationUnitDiscardFunction)(
    void *Ctx, LLVMOrcJITDylibRef JD, LLVMOrcSymbolStringPoolEntryRef Symbol);
typedef void (*LLVMOrcMaterializationUnitDestroyFunction)(void *Ctx);
typedef struct LLVMOrcOpaqueResourceTracker *LLVMOrcResourceTrackerRef;
typedef struct LLVMOrcOpaqueDefinitionGenerator
    *LLVMOrcDefinitionGeneratorRef;
typedef struct LLVMOrcOpaqueLookupState *LLVMOrcLookupStateRef;
typedef LLVMErrorRef (*LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction)(
    LLVMOrcDefinitionGeneratorRef GeneratorObj, void *Ctx,
    LLVMOrcLookupStateRef *LookupState, LLVMOrcLookupKind Kind,
    LLVMOrcJITDylibRef JD, LLVMOrcJITDylibLookupFlags JDLookupFlags,
    LLVMOrcCLookupSet LookupSet, size_t LookupSetSize);
typedef void (*LLVMOrcDisposeCAPIDefinitionGeneratorFunction)(void *Ctx);
typedef int (*LLVMOrcSymbolPredicate)(void *Ctx,
                                      LLVMOrcSymbolStringPoolEntryRef Sym);
typedef struct LLVMOrcOpaqueThreadSafeContext *LLVMOrcThreadSafeContextRef;
typedef struct LLVMOrcOpaqueThreadSafeModule *LLVMOrcThreadSafeModuleRef;
typedef LLVMErrorRef (*LLVMOrcGenericIRModuleOperationFunction)(
    void *Ctx, LLVMModuleRef M);
typedef struct LLVMOrcOpaqueJITTargetMachineBuilder
    *LLVMOrcJITTargetMachineBuilderRef;
typedef struct LLVMOrcOpaqueObjectLayer *LLVMOrcObjectLayerRef;
typedef struct LLVMOrcOpaqueObjectLinkingLayer *LLVMOrcObjectLinkingLayerRef;
typedef struct LLVMOrcOpaqueIRTransformLayer *LLVMOrcIRTransformLayerRef;
typedef LLVMErrorRef (*LLVMOrcIRTransformLayerTransformFunction)(
    void *Ctx, LLVMOrcThreadSafeModuleRef *ModInOut,
    LLVMOrcMaterializationResponsibilityRef MR);
typedef struct LLVMOrcOpaqueObjectTransformLayer
    *LLVMOrcObjectTransformLayerRef;
typedef LLVMErrorRef (*LLVMOrcObjectTransformLayerTransformFunction)(
    void *Ctx, LLVMMemoryBufferRef *ObjInOut);
typedef struct LLVMOrcOpaqueIndirectStubsManager
    *LLVMOrcIndirectStubsManagerRef;
typedef struct LLVMOrcOpaqueLazyCallThroughManager
    *LLVMOrcLazyCallThroughManagerRef;
typedef struct LLVMOrcOpaqueDumpObjects *LLVMOrcDumpObjectsRef;
LLVM_C_ABI void LLVMOrcExecutionSessionSetErrorReporter(
    LLVMOrcExecutionSessionRef ES, LLVMOrcErrorReporterFunction ReportError,
    void *Ctx);
LLVM_C_ABI LLVMOrcSymbolStringPoolRef
LLVMOrcExecutionSessionGetSymbolStringPool(LLVMOrcExecutionSessionRef ES);
LLVM_C_ABI void
LLVMOrcSymbolStringPoolClearDeadEntries(LLVMOrcSymbolStringPoolRef SSP);
LLVM_C_ABI LLVMOrcSymbolStringPoolEntryRef
LLVMOrcExecutionSessionIntern(LLVMOrcExecutionSessionRef ES, const char *Name);
typedef void (*LLVMOrcExecutionSessionLookupHandleResultFunction)(
    LLVMErrorRef Err, LLVMOrcCSymbolMapPairs Result, size_t NumPairs,
    void *Ctx);
LLVM_C_ABI void LLVMOrcExecutionSessionLookup(
    LLVMOrcExecutionSessionRef ES, LLVMOrcLookupKind K,
    LLVMOrcCJITDylibSearchOrder SearchOrder, size_t SearchOrderSize,
    LLVMOrcCLookupSet Symbols, size_t SymbolsSize,
    LLVMOrcExecutionSessionLookupHandleResultFunction HandleResult, void *Ctx);
LLVM_C_ABI void
LLVMOrcRetainSymbolStringPoolEntry(LLVMOrcSymbolStringPoolEntryRef S);
LLVM_C_ABI void
LLVMOrcReleaseSymbolStringPoolEntry(LLVMOrcSymbolStringPoolEntryRef S);
LLVM_C_ABI const char *
LLVMOrcSymbolStringPoolEntryStr(LLVMOrcSymbolStringPoolEntryRef S);
LLVM_C_ABI void LLVMOrcReleaseResourceTracker(LLVMOrcResourceTrackerRef RT);
LLVM_C_ABI void
LLVMOrcResourceTrackerTransferTo(LLVMOrcResourceTrackerRef SrcRT,
                                 LLVMOrcResourceTrackerRef DstRT);
LLVM_C_ABI LLVMErrorRef
LLVMOrcResourceTrackerRemove(LLVMOrcResourceTrackerRef RT);
LLVM_C_ABI void
LLVMOrcDisposeDefinitionGenerator(LLVMOrcDefinitionGeneratorRef DG);
LLVM_C_ABI void
LLVMOrcDisposeMaterializationUnit(LLVMOrcMaterializationUnitRef MU);
LLVM_C_ABI LLVMOrcMaterializationUnitRef LLVMOrcCreateCustomMaterializationUnit(
    const char *Name, void *Ctx, LLVMOrcCSymbolFlagsMapPairs Syms,
    size_t NumSyms, LLVMOrcSymbolStringPoolEntryRef InitSym,
    LLVMOrcMaterializationUnitMaterializeFunction Materialize,
    LLVMOrcMaterializationUnitDiscardFunction Discard,
    LLVMOrcMaterializationUnitDestroyFunction Destroy);
LLVM_C_ABI LLVMOrcMaterializationUnitRef
LLVMOrcAbsoluteSymbols(LLVMOrcCSymbolMapPairs Syms, size_t NumPairs);
LLVM_C_ABI LLVMOrcMaterializationUnitRef LLVMOrcLazyReexports(
    LLVMOrcLazyCallThroughManagerRef LCTM, LLVMOrcIndirectStubsManagerRef ISM,
    LLVMOrcJITDylibRef SourceRef, LLVMOrcCSymbolAliasMapPairs CallableAliases,
    size_t NumPairs);
// TODO: ImplSymbolMad SrcJDLoc
LLVM_C_ABI void LLVMOrcDisposeMaterializationResponsibility(
    LLVMOrcMaterializationResponsibilityRef MR);
LLVM_C_ABI LLVMOrcJITDylibRef
LLVMOrcMaterializationResponsibilityGetTargetDylib(
    LLVMOrcMaterializationResponsibilityRef MR);
LLVM_C_ABI LLVMOrcExecutionSessionRef
LLVMOrcMaterializationResponsibilityGetExecutionSession(
    LLVMOrcMaterializationResponsibilityRef MR);
LLVM_C_ABI LLVMOrcCSymbolFlagsMapPairs
LLVMOrcMaterializationResponsibilityGetSymbols(
    LLVMOrcMaterializationResponsibilityRef MR, size_t *NumPairs);
LLVM_C_ABI void
LLVMOrcDisposeCSymbolFlagsMap(LLVMOrcCSymbolFlagsMapPairs Pairs);
LLVM_C_ABI LLVMOrcSymbolStringPoolEntryRef
LLVMOrcMaterializationResponsibilityGetInitializerSymbol(
    LLVMOrcMaterializationResponsibilityRef MR);
LLVM_C_ABI LLVMOrcSymbolStringPoolEntryRef *
LLVMOrcMaterializationResponsibilityGetRequestedSymbols(
    LLVMOrcMaterializationResponsibilityRef MR, size_t *NumSymbols);
LLVM_C_ABI void LLVMOrcDisposeSymbols(LLVMOrcSymbolStringPoolEntryRef *Symbols);
LLVM_C_ABI LLVMErrorRef LLVMOrcMaterializationResponsibilityNotifyResolved(
    LLVMOrcMaterializationResponsibilityRef MR, LLVMOrcCSymbolMapPairs Symbols,
    size_t NumPairs);
LLVM_C_ABI LLVMErrorRef LLVMOrcMaterializationResponsibilityNotifyEmitted(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcCSymbolDependenceGroup *SymbolDepGroups, size_t NumSymbolDepGroups);
LLVM_C_ABI LLVMErrorRef LLVMOrcMaterializationResponsibilityDefineMaterializing(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcCSymbolFlagsMapPairs Pairs, size_t NumPairs);
LLVM_C_ABI void LLVMOrcMaterializationResponsibilityFailMaterialization(
    LLVMOrcMaterializationResponsibilityRef MR);
LLVM_C_ABI LLVMErrorRef LLVMOrcMaterializationResponsibilityReplace(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcMaterializationUnitRef MU);
LLVM_C_ABI LLVMErrorRef LLVMOrcMaterializationResponsibilityDelegate(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcSymbolStringPoolEntryRef *Symbols, size_t NumSymbols,
    LLVMOrcMaterializationResponsibilityRef *Result);
LLVM_C_ABI LLVMOrcJITDylibRef LLVMOrcExecutionSessionCreateBareJITDylib(
    LLVMOrcExecutionSessionRef ES, const char *Name);
LLVM_C_ABI LLVMErrorRef LLVMOrcExecutionSessionCreateJITDylib(
    LLVMOrcExecutionSessionRef ES, LLVMOrcJITDylibRef *Result,
    const char *Name);
LLVM_C_ABI LLVMOrcJITDylibRef LLVMOrcExecutionSessionGetJITDylibByName(
    LLVMOrcExecutionSessionRef ES, const char *Name);
LLVM_C_ABI LLVMOrcResourceTrackerRef
LLVMOrcJITDylibCreateResourceTracker(LLVMOrcJITDylibRef JD);
LLVM_C_ABI LLVMOrcResourceTrackerRef
LLVMOrcJITDylibGetDefaultResourceTracker(LLVMOrcJITDylibRef JD);
LLVM_C_ABI LLVMErrorRef LLVMOrcJITDylibDefine(LLVMOrcJITDylibRef JD,
                                              LLVMOrcMaterializationUnitRef MU);
LLVM_C_ABI LLVMErrorRef LLVMOrcJITDylibClear(LLVMOrcJITDylibRef JD);
LLVM_C_ABI void LLVMOrcJITDylibAddGenerator(LLVMOrcJITDylibRef JD,
                                            LLVMOrcDefinitionGeneratorRef DG);
LLVM_C_ABI LLVMOrcDefinitionGeneratorRef
LLVMOrcCreateCustomCAPIDefinitionGenerator(
    LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction F, void *Ctx,
    LLVMOrcDisposeCAPIDefinitionGeneratorFunction Dispose);
LLVM_C_ABI void LLVMOrcLookupStateContinueLookup(LLVMOrcLookupStateRef S,
                                                 LLVMErrorRef Err);
LLVM_C_ABI LLVMErrorRef LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(
    LLVMOrcDefinitionGeneratorRef *Result, char GlobalPrefx,
    LLVMOrcSymbolPredicate Filter, void *FilterCtx);
LLVM_C_ABI LLVMErrorRef LLVMOrcCreateDynamicLibrarySearchGeneratorForPath(
    LLVMOrcDefinitionGeneratorRef *Result, const char *FileName,
    char GlobalPrefix, LLVMOrcSymbolPredicate Filter, void *FilterCtx);
LLVM_C_ABI LLVMErrorRef LLVMOrcCreateStaticLibrarySearchGeneratorForPath(
    LLVMOrcDefinitionGeneratorRef *Result, LLVMOrcObjectLayerRef ObjLayer,
    const char *FileName);
LLVM_C_ABI LLVMOrcThreadSafeContextRef LLVMOrcCreateNewThreadSafeContext(void);
LLVM_C_ABI LLVMOrcThreadSafeContextRef
LLVMOrcCreateNewThreadSafeContextFromLLVMContext(LLVMContextRef Ctx);
LLVM_C_ABI void
LLVMOrcDisposeThreadSafeContext(LLVMOrcThreadSafeContextRef TSCtx);
LLVM_C_ABI LLVMOrcThreadSafeModuleRef LLVMOrcCreateNewThreadSafeModule(
    LLVMModuleRef M, LLVMOrcThreadSafeContextRef TSCtx);
LLVM_C_ABI void LLVMOrcDisposeThreadSafeModule(LLVMOrcThreadSafeModuleRef TSM);
LLVM_C_ABI LLVMErrorRef LLVMOrcThreadSafeModuleWithModuleDo(
    LLVMOrcThreadSafeModuleRef TSM, LLVMOrcGenericIRModuleOperationFunction F,
    void *Ctx);
LLVM_C_ABI LLVMErrorRef LLVMOrcJITTargetMachineBuilderDetectHost(
    LLVMOrcJITTargetMachineBuilderRef *Result);
LLVM_C_ABI LLVMOrcJITTargetMachineBuilderRef
LLVMOrcJITTargetMachineBuilderCreateFromTargetMachine(LLVMTargetMachineRef TM);
LLVM_C_ABI void
LLVMOrcDisposeJITTargetMachineBuilder(LLVMOrcJITTargetMachineBuilderRef JTMB);
LLVM_C_ABI char *LLVMOrcJITTargetMachineBuilderGetTargetTriple(
    LLVMOrcJITTargetMachineBuilderRef JTMB);
LLVM_C_ABI void LLVMOrcJITTargetMachineBuilderSetTargetTriple(
    LLVMOrcJITTargetMachineBuilderRef JTMB, const char *TargetTriple);
LLVM_C_ABI LLVMErrorRef LLVMOrcObjectLayerAddObjectFile(
    LLVMOrcObjectLayerRef ObjLayer, LLVMOrcJITDylibRef JD,
    LLVMMemoryBufferRef ObjBuffer);
LLVM_C_ABI LLVMErrorRef LLVMOrcObjectLayerAddObjectFileWithRT(
    LLVMOrcObjectLayerRef ObjLayer, LLVMOrcResourceTrackerRef RT,
    LLVMMemoryBufferRef ObjBuffer);
LLVM_C_ABI void
LLVMOrcObjectLayerEmit(LLVMOrcObjectLayerRef ObjLayer,
                       LLVMOrcMaterializationResponsibilityRef R,
                       LLVMMemoryBufferRef ObjBuffer);
LLVM_C_ABI void LLVMOrcDisposeObjectLayer(LLVMOrcObjectLayerRef ObjLayer);
LLVM_C_ABI void
LLVMOrcIRTransformLayerEmit(LLVMOrcIRTransformLayerRef IRTransformLayer,
                            LLVMOrcMaterializationResponsibilityRef MR,
                            LLVMOrcThreadSafeModuleRef TSM);
LLVM_C_ABI void LLVMOrcIRTransformLayerSetTransform(
    LLVMOrcIRTransformLayerRef IRTransformLayer,
    LLVMOrcIRTransformLayerTransformFunction TransformFunction, void *Ctx);
LLVM_C_ABI void LLVMOrcObjectTransformLayerSetTransform(
    LLVMOrcObjectTransformLayerRef ObjTransformLayer,
    LLVMOrcObjectTransformLayerTransformFunction TransformFunction, void *Ctx);
LLVM_C_ABI LLVMOrcIndirectStubsManagerRef
LLVMOrcCreateLocalIndirectStubsManager(const char *TargetTriple);
LLVM_C_ABI void
LLVMOrcDisposeIndirectStubsManager(LLVMOrcIndirectStubsManagerRef ISM);
LLVM_C_ABI LLVMErrorRef LLVMOrcCreateLocalLazyCallThroughManager(
    const char *TargetTriple, LLVMOrcExecutionSessionRef ES,
    LLVMOrcJITTargetAddress ErrorHandlerAddr,
    LLVMOrcLazyCallThroughManagerRef *LCTM);
LLVM_C_ABI void
LLVMOrcDisposeLazyCallThroughManager(LLVMOrcLazyCallThroughManagerRef LCTM);
LLVM_C_ABI LLVMOrcDumpObjectsRef
LLVMOrcCreateDumpObjects(const char *DumpDir, const char *IdentifierOverride);
LLVM_C_ABI void LLVMOrcDisposeDumpObjects(LLVMOrcDumpObjectsRef DumpObjects);
LLVM_C_ABI LLVMErrorRef LLVMOrcDumpObjects_CallOperator(
    LLVMOrcDumpObjectsRef DumpObjects, LLVMMemoryBufferRef *ObjBuffer);
LLVM_C_EXTERN_C_END
#endif 
