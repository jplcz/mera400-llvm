//===-- Mera400InstrInfo.cpp - MERA-400 instruction information -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Target-specific instruction queries used by the generic code generator:
// register copies and spills, instruction sizes, and branch analysis. Sizes
// matter because branch relaxation must know whether a short jump (which
// reaches only +-63 words) can reach its target.
//
//===----------------------------------------------------------------------===//

#include "Mera400InstrInfo.h"
#include "Mera400.h"
#include "Mera400Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "Mera400GenInstrInfo.inc"

void Mera400InstrInfo::anchor() {}

Mera400InstrInfo::Mera400InstrInfo(const Mera400Subtarget &STI)
    : Mera400GenInstrInfo(STI, RI, Mera400::ADJCALLSTACKDOWN,
                          Mera400::ADJCALLSTACKUP),
      RI() {}

void Mera400InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, Register DestReg,
                                   Register SrcReg, bool KillSrc,
                                   bool RenamableDest,
                                   bool RenamableSrc) const {
  if (!Mera400::GPR16RegClass.contains(DestReg, SrcReg))
    llvm_unreachable("Impossible reg-to-reg copy");
  BuildMI(MBB, I, DL, get(Mera400::MOVrr), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

static MachineMemOperand *getFrameMMO(MachineFunction &MF, int FI,
                                      MachineMemOperand::Flags Flags) {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, FI),
                                 Flags, MFI.getObjectSize(FI),
                                 MFI.getObjectAlign(FI));
}

// Spills and reloads are word stores and loads relative to R7; the frame index
// becomes an R7 displacement in Mera400RegisterInfo::eliminateFrameIndex.
void Mera400InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *, Register,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MBB.findDebugLoc(MI);
  BuildMI(MBB, MI, DL, get(Mera400::STWrm))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(
          getFrameMMO(*MBB.getParent(), FrameIndex, MachineMemOperand::MOStore))
      .setMIFlags(Flags);
}

void Mera400InstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
    int FrameIndex, const TargetRegisterClass *, Register, unsigned,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MBB.findDebugLoc(MI);
  BuildMI(MBB, MI, DL, get(Mera400::LDWrm), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(
          getFrameMMO(*MBB.getParent(), FrameIndex, MachineMemOperand::MOLoad))
      .setMIFlags(Flags);
}

// Size of the compare in a BR_CC/BR_CCI: "cw"/"cl" with a register, "cwt" with
// a small constant, otherwise the two-word form.
static unsigned getCompareSize(const MachineInstr &MI) {
  if (MI.getOpcode() == Mera400::BR_CC)
    return 2;
  auto CC = static_cast<Mera400::CondCode>(MI.getOperand(2).getImm());
  return Mera400::usesShortCompare(
             CC, static_cast<int16_t>(MI.getOperand(1).getImm()))
             ? 2
             : 4;
}

// A BR_CC is one pseudo but prints as a compare plus one or two jumps: there is
// no single jump for "less or equal" or "greater or equal", so those use two
// ("jl"+"je", "jg"+"je"). "jn" (not equal) has no short form.
static unsigned getJumpsSize(Mera400::CondCode CC) {
  unsigned Jumps = (CC == Mera400::CC_LE || CC == Mera400::CC_GE ||
                    CC == Mera400::CC_ULE || CC == Mera400::CC_UGE)
                       ? 2
                       : 1;
  unsigned JumpSize =
      Mera400::UseShortJumps && CC != Mera400::CC_NE ? 2 : 4;
  return Jumps * JumpSize;
}

static bool isShortImmOperand(const MachineOperand &MO) {
  return MO.isImm() && Mera400::isShortImm(static_cast<int16_t>(MO.getImm()));
}

// Pseudo instructions expand to different numbers of words depending on their
// operands, so the size is computed here rather than taken from the fixed Size
// in TableGen. Branch relaxation and the assembler layout depend on it being
// exact (or an over-estimate where a value is not yet known).
unsigned Mera400InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case Mera400::BR_CC:
  case Mera400::BR_CCI:
    return getCompareSize(MI) +
           getJumpsSize(static_cast<Mera400::CondCode>(MI.getOperand(2).getImm()));
  case Mera400::JMP:
    return Mera400::UseShortJumps ? 2 : 4;
  case Mera400::JMPL:
    return 4;
  // A symbol or frame index operand is only known later: assume the long form.
  case Mera400::MOVri:
    return isShortImmOperand(MI.getOperand(1)) ? 2 : 4;
  case Mera400::ADDri:
    return isShortImmOperand(MI.getOperand(2)) ? 2 : 4;
  case Mera400::LDWrm:
  case Mera400::STWrm:
  case Mera400::LDBrm:
  case Mera400::STBrm:
  case Mera400::AFrm:
  case Mera400::SFrm:
  case Mera400::MFrm:
  case Mera400::DFrm: {
    // The displacement of a frame index is not known yet: assume the long form.
    unsigned BaseIdx = MI.getOpcode() >= Mera400::AFrm &&
                               MI.getOpcode() <= Mera400::DFrm
                           ? 0
                           : 1;
    bool Short = MI.getOperand(BaseIdx).isReg() &&
                 MI.getOperand(BaseIdx + 1).isImm() &&
                 MI.getOperand(BaseIdx + 1).getImm() == 0;
    return (Short ? 2 : 4) + (MI.getOpcode() == Mera400::LDBrm ? 2 : 0);
  }
  default:
    return get(MI.getOpcode()).getSize();
  }
}

static bool isCondBranch(const MachineInstr &MI) {
  return MI.getOpcode() == Mera400::BR_CC || MI.getOpcode() == Mera400::BR_CCI;
}

static bool isUncondBranch(const MachineInstr &MI) {
  return MI.getOpcode() == Mera400::JMP || MI.getOpcode() == Mera400::JMPL;
}

MachineBasicBlock *
Mera400InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  assert(MI.getDesc().isBranch() && "not a branch");
  return MI.getOperand(MI.getNumExplicitOperands() - 1).getMBB();
}

// The condition is the condition code followed by the two compared operands,
// so that insertBranch can recreate BR_CC or BR_CCI.
static void parseCondBranch(const MachineInstr &MI, MachineBasicBlock *&Target,
                            SmallVectorImpl<MachineOperand> &Cond) {
  Target = MI.getOperand(3).getMBB();
  Cond.push_back(MachineOperand::CreateImm(MI.getOperand(2).getImm()));
  Cond.push_back(MachineOperand::CreateReg(MI.getOperand(0).getReg(), false));
  const MachineOperand &B = MI.getOperand(1);
  Cond.push_back(B.isReg() ? MachineOperand::CreateReg(B.getReg(), false)
                           : MachineOperand::CreateImm(B.getImm()));
}

bool Mera400InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  TBB = FBB = nullptr;
  Cond.clear();

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isUnpredicatedTerminator(*I))
    return false;

  // Find the first unconditional branch; everything after it is dead.
  MachineBasicBlock::iterator FirstUncond = MBB.end();
  unsigned NumTerminators = 0;
  for (auto J = I.getReverse(); J != MBB.rend() && isUnpredicatedTerminator(*J);
       ++J) {
    ++NumTerminators;
    if (isUncondBranch(*J))
      FirstUncond = J.getReverse();
  }
  if (AllowModify && FirstUncond != MBB.end()) {
    while (std::next(FirstUncond) != MBB.end())
      std::next(FirstUncond)->eraseFromParent();
    I = FirstUncond;
    NumTerminators = 1 + (FirstUncond != MBB.begin() &&
                          isCondBranch(*std::prev(FirstUncond)));
  }

  if (NumTerminators > 2)
    return true;
  if (NumTerminators == 1) {
    if (isUncondBranch(*I)) {
      TBB = getBranchDestBlock(*I);
      return false;
    }
    if (isCondBranch(*I)) {
      parseCondBranch(*I, TBB, Cond);
      return false;
    }
    return true;
  }
  MachineBasicBlock::iterator Prev = std::prev(I);
  if (isCondBranch(*Prev) && isUncondBranch(*I)) {
    parseCondBranch(*Prev, TBB, Cond);
    FBB = getBranchDestBlock(*I);
    return false;
  }
  return true;
}

unsigned Mera400InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                        int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;
  unsigned Count = 0;
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!isCondBranch(*I) && !isUncondBranch(*I))
      break;
    if (BytesRemoved)
      *BytesRemoved += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }
  return Count;
}

unsigned Mera400InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                        MachineBasicBlock *TBB,
                                        MachineBasicBlock *FBB,
                                        ArrayRef<MachineOperand> Cond,
                                        const DebugLoc &DL,
                                        int *BytesAdded) const {
  assert(TBB && "insertBranch needs a destination");
  assert((Cond.size() == 3 || Cond.empty()) && "invalid branch condition");
  if (BytesAdded)
    *BytesAdded = 0;
  unsigned Count = 0;
  auto Insert = [&](MachineInstrBuilder MIB) {
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(*MIB);
    ++Count;
  };

  if (Cond.empty()) {
    Insert(BuildMI(&MBB, DL, get(Mera400::JMP)).addMBB(TBB));
    return Count;
  }
  bool IsImm = Cond[2].isImm();
  MachineInstrBuilder MIB =
      BuildMI(&MBB, DL, get(IsImm ? Mera400::BR_CCI : Mera400::BR_CC))
          .addReg(Cond[1].getReg());
  if (IsImm)
    MIB.addImm(Cond[2].getImm());
  else
    MIB.addReg(Cond[2].getReg());
  MIB.addImm(Cond[0].getImm()).addMBB(TBB);
  Insert(MIB);
  if (FBB)
    Insert(BuildMI(&MBB, DL, get(Mera400::JMP)).addMBB(FBB));
  return Count;
}

bool Mera400InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 3 && "invalid branch condition");
  Cond[0].setImm(Mera400::invertCC(
      static_cast<Mera400::CondCode>(Cond[0].getImm())));
  return false;
}

// Offsets are in bytes from the start of the branch to the start of the
// destination block. A short jump counts words from the next instruction and
// reaches -63..63 words. A BR_CC may need two short jumps after a compare of
// up to 4 bytes, so the first jump is at most 6 bytes and the last at least 2
// bytes from the start of the pseudo instruction: allow for both.
bool Mera400InstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                             int64_t BrOffset) const {
  if (!Mera400::UseShortJumps)
    return true;
  switch (BranchOpc) {
  case Mera400::JMP:
    return BrOffset >= -2 * 62 && BrOffset <= 2 * 64;
  case Mera400::BR_CC:
  case Mera400::BR_CCI:
    return BrOffset >= -2 * 63 + 8 && BrOffset <= 2 * 63 + 4;
  default:
    return true;
  }
}

// A branch the "uj" form can always reach: the destination is a 16-bit word
// address.
void Mera400InstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                            MachineBasicBlock &NewDestBB,
                                            MachineBasicBlock &, const DebugLoc &DL,
                                            int64_t, RegScavenger *) const {
  BuildMI(&MBB, DL, get(Mera400::JMPL)).addMBB(&NewDestBB);
}
