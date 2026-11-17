//===-- Mera400TargetMachine.cpp - Define TargetMachine for MERA-400
//---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Builds the code generation pipeline for the legacy pass manager. The
// target-specific steps are pointer lowering before instruction selection
// (ISel) and branch relaxation after it.
//
//===----------------------------------------------------------------------===//

#include "Mera400TargetMachine.h"
#include "Mera400.h"
#include "Mera400MachineFunctionInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"

using namespace llvm;

// Static relocation model by default: code is linked at a fixed address and
// there is no operating system to relocate it. The default code model is the
// small one.
static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

Mera400TargetMachine::Mera400TargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

Mera400TargetMachine::~Mera400TargetMachine() = default;

MachineFunctionInfo *Mera400TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return new (Allocator.Allocate<Mera400MachineFunctionInfo>())
      Mera400MachineFunctionInfo(F, STI);
}

namespace {
class Mera400PassConfig : public TargetPassConfig {
public:
  Mera400PassConfig(Mera400TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  Mera400TargetMachine &getMera400TargetMachine() const {
    return getTM<Mera400TargetMachine>();
  }

  bool addInstSelector() override;
  bool addPreISel() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *Mera400TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new Mera400PassConfig(*this, PM);
}

// Pointer lowering must run before ISel while the IR still has GEPs; branch
// relaxation runs last, when instruction sizes are final, and replaces short
// jumps that cannot reach their target.
bool Mera400PassConfig::addPreISel() {
  addPass(createMera400LowerGlobalSpacesLegacyPass());
  addPass(createMera400LowerPointersLegacyPass());
  return false;
}

bool Mera400PassConfig::addInstSelector() {
  addPass(createMera400ISelDag(getMera400TargetMachine(), getOptLevel()));
  return false;
}

void Mera400PassConfig::addPreEmitPass() { addPass(&BranchRelaxationPassID); }
