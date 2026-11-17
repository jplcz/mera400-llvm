//===-- Mera400Target.cpp - MERA-400 target initialization ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Initialization hook that registers the target machine and its passes. The
// tools (llc and others) call LLVMInitializeMera400Target at startup.
//
//===----------------------------------------------------------------------===//

#include "Mera400.h"
#include "Mera400TargetMachine.h"
#include "TargetInfo/Mera400TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMera400Target() {
  RegisterTargetMachine<Mera400TargetMachine> X(getTheMera400Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeMera400AsmPrinterPass(PR);
  initializeMera400DAGToDAGISelLegacyPass(PR);
  initializeMera400LowerPointersLegacyPass(PR);
  initializeMera400LowerGlobalSpacesLegacyPass(PR);
}
