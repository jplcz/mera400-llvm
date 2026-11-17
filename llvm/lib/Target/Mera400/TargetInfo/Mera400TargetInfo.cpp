//===-- Mera400TargetInfo.cpp - MERA-400 target registration --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Entry point that registers the MERA-400 target with LLVM's target registry.
//
//===----------------------------------------------------------------------===//

#include "Mera400TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheMera400Target() {
  static Target T;
  return T;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMera400TargetInfo() {
  RegisterTarget<Triple::mera400> X(getTheMera400Target(), "mera400",
                                    "MERA-400 [experimental]", "Mera400");
}
