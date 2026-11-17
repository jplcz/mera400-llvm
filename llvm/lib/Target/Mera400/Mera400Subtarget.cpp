//===-- Mera400Subtarget.cpp - MERA-400 subtarget information
//--------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Creates the subtarget and parses its feature string (for example
// "+large-memory"); the tables come from TableGen.
//
//===----------------------------------------------------------------------===//

#include "Mera400Subtarget.h"
#include "Mera400.h"
#include "Mera400SelectionDAGInfo.h"
#include "Mera400TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "mera400-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "Mera400GenSubtargetInfo.inc"

void Mera400Subtarget::anchor() {}

Mera400Subtarget &
Mera400Subtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  StringRef CPUName = CPU.empty() ? "generic" : CPU;
  ParseSubtargetFeatures(CPUName, /*TuneCPU=*/CPUName, FS);
  return *this;
}

Mera400Subtarget::Mera400Subtarget(const Triple &TT, const std::string &CPU,
                                   const std::string &FS,
                                   const Mera400TargetMachine &TM)
    : Mera400GenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this) {
  TSInfo = std::make_unique<Mera400SelectionDAGInfo>();
}

Mera400Subtarget::~Mera400Subtarget() = default;

const SelectionDAGTargetInfo *Mera400Subtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
