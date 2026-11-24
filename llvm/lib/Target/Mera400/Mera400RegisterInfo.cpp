//===-- Mera400RegisterInfo.cpp - MERA-400 register information -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registers need no code beyond which of them the allocator may use, and how
// stack objects are addressed.
//
//===----------------------------------------------------------------------===//

#include "Mera400RegisterInfo.h"
#include "Mera400.h"
#include "Mera400FrameLowering.h"
#include "Mera400Subtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "Mera400GenRegisterInfo.inc"

Mera400RegisterInfo::Mera400RegisterInfo()
    : Mera400GenRegisterInfo(Mera400::IC) {}

const MCPhysReg *
Mera400RegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  return CSR_Mera400_Frame_SaveList;
}

const uint32_t *
Mera400RegisterInfo::getCallPreservedMask(const MachineFunction &,
                                          CallingConv::ID) const {
  return CSR_Mera400_RegMask;
}

BitVector Mera400RegisterInfo::getReservedRegs(const MachineFunction &) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(Mera400::R0); // flags
  Reserved.set(Mera400::R4); // return address, saved in the frame by non-leaf functions
  Reserved.set(Mera400::R7); // stack pointer
  Reserved.set(Mera400::IC); // only a DWARF column, never allocated
  return Reserved;
}

// Frame indices appear only as the base of LDWrm/STWrm and LEA. Offsets of
// frame objects are in bytes, R7-relative displacements in words.
bool Mera400RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                              int, unsigned FIOperandNum,
                                              RegScavenger *) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getMF();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  assert(FIOperandNum + 1 < MI.getNumOperands() &&
         MI.getOperand(FIOperandNum + 1).isImm() &&
         "frame index must be followed by a displacement");

  int FI = MI.getOperand(FIOperandNum).getIndex();
  int64_t ByteOffset = MFI.getObjectOffset(FI) + MFI.getStackSize();
  assert(ByteOffset % 2 == 0 && "stack objects are word aligned");
  int64_t Disp = ByteOffset / 2 + MI.getOperand(FIOperandNum + 1).getImm();
  if (!isInt<16>(Disp))
    report_fatal_error("Mera400: stack frame is too large to address");

  if (MI.getOpcode() == Mera400::LEA) {
    DebugLoc DL = MI.getDebugLoc();
    MachineBasicBlock &MBB = *MI.getParent();
    Register Dst = MI.getOperand(0).getReg();
    BuildMI(MBB, II, DL, TII.get(Mera400::MOVrr), Dst).addReg(Mera400::R7);
    if (Disp != 0)
      BuildMI(MBB, II, DL, TII.get(Mera400::ADDri), Dst)
          .addReg(Dst)
          .addImm(Disp);
    MI.eraseFromParent();
    return true;
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(Mera400::R7, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Disp);
  return false;
}

Register Mera400RegisterInfo::getFrameRegister(const MachineFunction &) const {
  return Mera400::R7;
}
