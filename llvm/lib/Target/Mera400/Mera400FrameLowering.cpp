//===-- Mera400FrameLowering.cpp - MERA-400 frame lowering
//-----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Prologue and epilogue: the stack pointer R7 is moved once on entry and exit
// by the whole frame size. There is no frame pointer, so locals are addressed
// as R7 + word offset. R4 (the return address, stored by "rj") is saved in the
// frame only by functions that make calls.
//
//===----------------------------------------------------------------------===//

#include "Mera400FrameLowering.h"
#include "Mera400.h"
#include "Mera400InstrInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"

using namespace llvm;

// R7 holds a word address, so the frame is adjusted in words.
static void adjustStack(MachineFunction &MF, MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator MBBI, int64_t Words) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  BuildMI(MBB, MBBI, DL, TII.get(Mera400::ADDri), Mera400::R7)
      .addReg(Mera400::R7)
      .addImm(Words)
      .setMIFlag(MachineInstr::FrameSetup);
}

static bool checkFrameSize(MachineFunction &MF) {
  uint64_t Size = MF.getFrameInfo().getStackSize();
  if (Size / 2 <= 0x7fff)
    return true;
  MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported(
      MF.getFunction(), "Mera400: stack frame is larger than 32767 words"));
  return false;
}

void Mera400FrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  uint64_t Size = MF.getFrameInfo().getStackSize();
  if (Size == 0 || !checkFrameSize(MF))
    return;
  adjustStack(MF, MBB, MBB.begin(), -static_cast<int64_t>(Size / 2));
}

void Mera400FrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  uint64_t Size = MF.getFrameInfo().getStackSize();
  if (Size == 0 || Size / 2 > 0x7fff)
    return;
  adjustStack(MF, MBB, MBB.getFirstTerminator(), Size / 2);
}

void Mera400FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                    BitVector &SavedRegs,
                                                    RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  // A call overwrites R4, which holds this function's return address.
  if (MF.getFrameInfo().hasCalls())
    SavedRegs.set(Mera400::R4);
}

void Mera400FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  // Every object starts on a word boundary because R7 offsets count words.
  for (int I = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd(); I != E;
       ++I) {
    if (MFI.isDeadObjectIndex(I))
      continue;
    if (MFI.getObjectAlign(I) > Align(2)) {
      MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported(
          MF.getFunction(),
          "Mera400: stack objects aligned to more than one word are not "
          "supported"));
      return;
    }
    if (!MFI.isFixedObjectIndex(I) && MFI.getObjectAlign(I) < Align(2))
      MFI.setObjectAlignment(I, Align(2));
  }
}

MachineBasicBlock::iterator Mera400FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  // The call frame is part of the function frame, so nothing moves R7.
  return MBB.erase(MI);
}
