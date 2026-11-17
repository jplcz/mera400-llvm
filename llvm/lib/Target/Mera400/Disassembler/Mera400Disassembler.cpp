//===-- Mera400Disassembler.cpp - MERA-400 disassembler -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Decodes big-endian 16-bit MERA-400 instruction words. Words that are not a
// valid instruction (or whose operand would not assemble back to the same
// word, such as the sign-magnitude "-0") are reported as invalid so data in a
// code section is never shown as a misleading instruction.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Mera400MCTargetDesc.h"
#include "TargetInfo/Mera400TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;

namespace {
class Mera400Disassembler : public MCDisassembler {
public:
  Mera400Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

struct FixedEntry {
  uint16_t Word;
  unsigned Opcode;
  bool Mx16Only;
};
} // namespace

static const FixedEntry FixedInsts[] = {
    {0xe000, Mera400::NOP, false}, {0xec40, Mera400::MCL, false},
    {0xec80, Mera400::CIT, false}, {0xec81, Mera400::SIL, false},
    {0xec82, Mera400::SIU, false}, {0xec83, Mera400::SIT, false},
    {0xecc0, Mera400::GIU, false}, {0xed00, Mera400::LIP, false},
    {0xeec0, Mera400::GIL, false}, {0xed40, Mera400::CRON, true},
    {0xec84, Mera400::SINT, true}, {0xee84, Mera400::SIND, true},
};

static const unsigned ShortInsts[8] = {
    Mera400::AWT, Mera400::TRB, Mera400::IRB, Mera400::DRB,
    Mera400::CWT, Mera400::LWT, Mera400::LWS, Mera400::RWS,
};

static const unsigned ShortBranches[8] = {
    Mera400::UJS, Mera400::JLS, Mera400::JES, Mera400::JGS,
    Mera400::JVS, Mera400::JXS, Mera400::JYS, Mera400::JCS,
};

static const unsigned ByteInsts[4] = {
    Mera400::BLC, Mera400::EXL, Mera400::BRC, Mera400::NRF,
};

static const unsigned RegInsts[2][10] = {
    {Mera400::RIC, Mera400::ZLB, Mera400::SXU, Mera400::NGA, Mera400::SLZ,
     Mera400::SLY, Mera400::SLX, Mera400::SRY, Mera400::NGL, Mera400::RPC},
    {Mera400::RKY, Mera400::ZRB, Mera400::SXL, Mera400::NGC, Mera400::SVZ,
     Mera400::SVY, Mera400::SVX, Mera400::SRX, Mera400::SRZ, Mera400::LPC},
};

// Normal-argument instructions with a register field: one-word form first,
// then the two-word form. Indexed by opcode - 0x10 (0x10..0x2f); 0x1f is
// the register-less group below.
#define NORM(Name) {Mera400::Name, Mera400::Name##_EXT}
struct NormPair {
  unsigned Short, Ext;
};
static const NormPair NormRegInsts[32] = {
    NORM(LW), NORM(TW), NORM(LS), NORM(RI), NORM(RW), NORM(PW), NORM(RJ),
    NORM(IS), NORM(BB), NORM(BM), NORM(BS), NORM(BC), NORM(BN), NORM(OU),
    NORM(IN), {0, 0}, NORM(AW), NORM(AC), NORM(SW), NORM(CW), NORM(OR),
    NORM(OM), NORM(NR), NORM(NM), NORM(ER), NORM(EM), NORM(XR), NORM(XM),
    NORM(CL), NORM(LB), NORM(RB), NORM(CB),
};

// Normal-argument instructions whose register field selects the operation:
// opcodes 0x1f, 0x3c, 0x3d, 0x3e and 0x3f, eight selectors each.
static const NormPair NormNoRegInsts[5][8] = {
    {NORM(AD), NORM(SD), NORM(MW), NORM(DW), NORM(AF), NORM(SF), NORM(MF),
     NORM(DF)},
    {NORM(UJ), NORM(JL), NORM(JE), NORM(JG), NORM(JZ), NORM(JM), NORM(JN),
     NORM(LJ)},
    {NORM(LD), NORM(LF), NORM(LA), NORM(LL), NORM(TD), NORM(TF), NORM(TA),
     NORM(TL)},
    {NORM(RD), NORM(RF), NORM(RA), NORM(RL), NORM(PD), NORM(PF), NORM(PA),
     NORM(PL)},
    {NORM(MB), NORM(IM), NORM(KI), NORM(FI), NORM(SP), NORM(MD), NORM(RZ),
     NORM(IB)},
};
#undef NORM

static MCOperand reg(unsigned N) {
  return MCOperand::createReg(MCRegister(Mera400::R0 + N));
}

// Short arguments are sign-magnitude: bit 9 is the sign, bits 5-0 the
// magnitude. "-0" has no assembly spelling, so it is rejected.
static bool decodeShortImm(uint16_t W, int64_t &Imm) {
  unsigned Magnitude = W & 0x3f;
  if (W & 0x200) {
    if (!Magnitude)
      return false;
    Imm = -static_cast<int64_t>(Magnitude);
  } else {
    Imm = Magnitude;
  }
  return true;
}

MCDisassembler::DecodeStatus
Mera400Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                    ArrayRef<uint8_t> Bytes, uint64_t,
                                    raw_ostream &) const {
  if (Bytes.size() < 2) {
    Size = Bytes.size();
    return Fail;
  }
  Size = 2;
  uint16_t W = support::endian::read16be(Bytes.data());
  unsigned Opc = W >> 10;
  unsigned Reg = (W >> 6) & 7;
  bool Mx16 = STI.hasFeature(Mera400::FeatureMX16);

  for (const FixedEntry &F : FixedInsts) {
    if (W != F.Word)
      continue;
    if (F.Mx16Only && !Mx16)
      return Fail;
    MI.setOpcode(F.Opcode);
    return Success;
  }

  int64_t Imm;
  if (Opc >= 0x30 && Opc <= 0x37) {
    if (!decodeShortImm(W, Imm))
      return Fail;
    MI.setOpcode(ShortInsts[Opc & 7]);
    MI.addOperand(reg(Reg));
    MI.addOperand(MCOperand::createImm(Imm));
    return Success;
  }

  switch (Opc) {
  case 0x38:
    if (!decodeShortImm(W, Imm))
      return Fail;
    MI.setOpcode(ShortBranches[Reg]);
    MI.addOperand(MCOperand::createImm(Imm));
    return Success;
  case 0x39: {
    unsigned Kind = (W >> 8) & 3;
    MI.setOpcode(ByteInsts[Kind]);
    unsigned Byte = W & 0xff;
    // BLC's operand is the byte shifted into the left half of the word.
    MI.addOperand(MCOperand::createImm(Kind == 0 ? Byte << 8 : Byte));
    return Success;
  }
  case 0x3a: {
    unsigned Variant = (W >> 9) & 1;
    if (((W >> 3) & 7) == 2) {
      unsigned Count = (Variant << 3) | (W & 7);
      MI.setOpcode(Mera400::SHC);
      MI.addOperand(reg(Reg));
      MI.addOperand(MCOperand::createImm(Count >= 8 ? int(Count) - 16
                                                    : int(Count)));
      return Success;
    }
    unsigned Op = W & 0x3f;
    if (Op >= 10)
      return Fail;
    MI.setOpcode(RegInsts[Variant][Op]);
    MI.addOperand(reg(Reg));
    return Success;
  }
  case 0x3b:
    if (Reg != 0)
      return Fail;
    MI.setOpcode(Mera400::HLT);
    MI.addOperand(MCOperand::createImm(((W >> 9) & 1) << 6 | (W & 0x3f)));
    return Success;
  default:
    break;
  }

  const NormPair *Pair = nullptr;
  bool HasReg = false;
  if (Opc == 0x1f)
    Pair = &NormNoRegInsts[0][Reg];
  else if (Opc >= 0x3c)
    Pair = &NormNoRegInsts[1 + (Opc - 0x3c)][Reg];
  else if (Opc >= 0x10 && Opc <= 0x2f) {
    Pair = &NormRegInsts[Opc - 0x10];
    HasReg = true;
  }
  if (!Pair)
    return Fail;

  // Control field: bit 6 indirect, bits 5-3 index register B, bits 2-0
  // register C; C = 0 means a 16-bit value follows in the next word.
  unsigned Control = ((W >> 9) & 1) << 6 | (W & 0x3f);
  bool Extended = (Control & 7) == 0;
  if (Extended) {
    if (Bytes.size() < 4)
      return Fail;
    MI.setOpcode(Pair->Ext);
  } else {
    MI.setOpcode(Pair->Short);
  }
  if (HasReg)
    MI.addOperand(reg(Reg));
  MI.addOperand(MCOperand::createImm(Control));
  if (Extended) {
    MI.addOperand(MCOperand::createImm(support::endian::read16be(&Bytes[2])));
    Size = 4;
  }
  return Success;
}

static MCDisassembler *createMera400Disassembler(const Target &,
                                                 const MCSubtargetInfo &STI,
                                                 MCContext &Ctx) {
  return new Mera400Disassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMera400Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheMera400Target(),
                                         createMera400Disassembler);
}
