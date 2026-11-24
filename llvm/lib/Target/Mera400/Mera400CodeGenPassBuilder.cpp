//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The same pipeline as Mera400TargetMachine.cpp, for the new pass manager
// (CodeGenPassBuilder). Both must stay in sync.
//
//===----------------------------------------------------------------------===//

#include "Mera400.h"
#include "Mera400AsmPrinter.h"
#include "Mera400TargetMachine.h"

#include "llvm/CodeGen/BranchRelaxation.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Passes/CodeGenPassBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Target/CGPassBuilderOption.h"

using namespace llvm;

namespace {

class Mera400CodeGenPassBuilder : public CodeGenPassBuilder {
  Mera400TargetMachine &getTM() const {
    return static_cast<Mera400TargetMachine &>(TM);
  }

public:
  explicit Mera400CodeGenPassBuilder(Mera400TargetMachine &TM,
                                     const CGPassBuilderOption &Opts,
                                     PassInstrumentationCallbacks *PIC)
      : CodeGenPassBuilder(TM, Opts, PIC) {}

  void addPreISel(PassManagerWrapper &PMW) override;
  Error addInstSelector(PassManagerWrapper &PMW) override;
  void addPreEmitPass(PassManagerWrapper &PMW) override;
  void addAsmPrinterBegin(PassManagerWrapper &PMW) override;
  void addAsmPrinter(PassManagerWrapper &PMW) override;
  void addAsmPrinterEnd(PassManagerWrapper &PMW) override;
};

void Mera400CodeGenPassBuilder::addPreISel(PassManagerWrapper &PMW) {
  // Function passes queued before this point must run before a module pass.
  flushFPMsToMPM(PMW);
  addModulePass(Mera400LowerGlobalSpacesPass(), PMW);
  addFunctionPass(Mera400LowerPointersPass(), PMW);
}

Error Mera400CodeGenPassBuilder::addInstSelector(PassManagerWrapper &PMW) {
  addMachineFunctionPass(Mera400ISelDAGToDAGPass(getTM(), getOptLevel()), PMW);
  return Error::success();
}

void Mera400CodeGenPassBuilder::addPreEmitPass(PassManagerWrapper &PMW) {
  addMachineFunctionPass(BranchRelaxationPass(), PMW);
}

void Mera400CodeGenPassBuilder::addAsmPrinterBegin(PassManagerWrapper &PMW) {
  addModulePass(Mera400AsmPrinterBeginPass(), PMW, /*Force=*/true);
}

void Mera400CodeGenPassBuilder::addAsmPrinter(PassManagerWrapper &PMW) {
  addMachineFunctionPass(Mera400AsmPrinterPass(), PMW);
}

void Mera400CodeGenPassBuilder::addAsmPrinterEnd(PassManagerWrapper &PMW) {
  addModulePass(Mera400AsmPrinterEndPass(), PMW);
}

} // namespace

void Mera400TargetMachine::registerPassBuilderCallbacks(PassBuilder &PB){
#define GET_PASS_REGISTRY "Mera400PassRegistry.def"
#include "llvm/Passes/TargetPassRegistry.inc"
  // Before any optimization has a chance to forward words stored to memory
  // into a double load as an IEEE bit pattern (see Mera400PunnedFloats.cpp).
  PB.registerPipelineStartEPCallback(
      [](ModulePassManager &MPM, OptimizationLevel) {
        MPM.addPass(Mera400PunnedFloatsPass());
      });
}

Error Mera400TargetMachine::buildCodeGenPipeline(
    ModulePassManager &MPM, ModuleAnalysisManager &MAM, raw_pwrite_stream &Out,
    raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
    const CGPassBuilderOption &Opt, MCContext &Ctx,
    PassInstrumentationCallbacks *PIC) {
  auto CGPB = Mera400CodeGenPassBuilder(*this, Opt, PIC);
  return CGPB.buildPipeline(MPM, MAM, Out, DwoOut, FileType, Ctx);
}
