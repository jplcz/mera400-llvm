//===-- Mera400AsmPrinter.h - MERA-400 assembly printer passes ---*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the passes that print a module, one function at a time, for the new
// pass manager's code generation pipeline (see Mera400CodeGenPassBuilder.cpp).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400ASMPRINTER_H
#define LLVM_LIB_TARGET_MERA400_MERA400ASMPRINTER_H

#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Mera400AsmPrinterBeginPass
    : public RequiredPassInfoMixin<Mera400AsmPrinterBeginPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

class Mera400AsmPrinterPass
    : public RequiredPassInfoMixin<Mera400AsmPrinterPass> {
public:
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};

class Mera400AsmPrinterEndPass
    : public RequiredPassInfoMixin<Mera400AsmPrinterEndPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif
