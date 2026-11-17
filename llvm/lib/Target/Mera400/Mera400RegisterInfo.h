//===-- Mera400RegisterInfo.h - MERA-400 register information --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Describes the registers and ABI-related register sets: R0 (flags) and R7
// (stack pointer) are reserved, R5/R6 are callee-saved, and frame indexes are
// replaced by R7-relative word offsets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400REGISTERINFO_H
#define LLVM_LIB_TARGET_MERA400_MERA400REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "Mera400GenRegisterInfo.inc"

namespace llvm {

class Mera400RegisterInfo : public Mera400GenRegisterInfo {
public:
  Mera400RegisterInfo();

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;
  Register getFrameRegister(const MachineFunction &MF) const override;
};

} // end namespace llvm

#endif
