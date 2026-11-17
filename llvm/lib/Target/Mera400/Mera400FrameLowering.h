//===-- Mera400FrameLowering.h - MERA-400 frame lowering --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Describes the stack frame layout: stack grows down, 2-byte aligned, no frame
// pointer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400FRAMELOWERING_H
#define LLVM_LIB_TARGET_MERA400_MERA400FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

// The stack grows downward and is addressed through R7; there is no frame
// pointer. The frame includes the area for outgoing call arguments, so R7 is
// adjusted only in the prologue and epilogue and callers never clean up.
class Mera400FrameLowering : public TargetFrameLowering {
protected:
  bool hasFPImpl(const MachineFunction &MF) const override { return false; }

public:
  Mera400FrameLowering()
      : TargetFrameLowering(StackGrowsDown, Align(2), 0, Align(2)) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                                RegScavenger *RS = nullptr) const override;
  void processFunctionBeforeFrameFinalized(
      MachineFunction &MF, RegScavenger *RS = nullptr) const override;
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;
};

} // end namespace llvm

#endif
