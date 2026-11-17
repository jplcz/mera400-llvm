//===-- Mera400Subtarget.h - MERA-400 subtarget information -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Holds per-target state: the optional MX-16 instructions and the large memory
// model, plus the instruction, lowering and frame objects that depend on them.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400SUBTARGET_H
#define LLVM_LIB_TARGET_MERA400_MERA400SUBTARGET_H

#include "Mera400FrameLowering.h"
#include "Mera400ISelLowering.h"
#include "Mera400InstrInfo.h"
#include "Mera400RegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <memory>
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "Mera400GenSubtargetInfo.inc"

namespace llvm {
class Mera400TargetMachine;

class Mera400Subtarget : public Mera400GenSubtargetInfo {
  virtual void anchor();
  bool HasMX16 = false;
  bool HasLargeMemory = false;
  bool HasAWP = false;
  Mera400InstrInfo InstrInfo;
  Mera400TargetLowering TLInfo;
  std::unique_ptr<const SelectionDAGTargetInfo> TSInfo;
  Mera400FrameLowering FrameLowering;

public:
  Mera400Subtarget(const Triple &TT, const std::string &CPU,
                   const std::string &FS, const Mera400TargetMachine &TM);
  ~Mera400Subtarget() override;

  Mera400Subtarget &initializeSubtargetDependencies(StringRef CPU,
                                                    StringRef FS);

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  bool hasMX16() const { return HasMX16; }
  bool hasLargeMemory() const { return HasLargeMemory; }
  bool hasAWP() const { return HasAWP; }

  const TargetFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const Mera400InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const Mera400RegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const Mera400TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override;
};
} // end namespace llvm

#endif
