//===-- Mera400MCCodeEmitter.cpp - MERA-400 instruction encoding ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Encodes MCInst (the machine code layer's instruction) into big-endian words.
// The bit layouts come from the TableGen instruction definitions
// (Mera400GenMCCodeEmitter.inc); the hooks here handle operands that need
// special encoding: sign-magnitude immediates, shift counts and the optional
// extra word of the two-word "normal argument" form.
//
//===----------------------------------------------------------------------===//

#include "Mera400MCExpr.h"
#include "Mera400MCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

namespace {
class Mera400MCCodeEmitter : public MCCodeEmitter {
  const MCInstrInfo &MCII;
  MCContext &Ctx;

  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &Op,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  uint64_t getShiftCountOpValue(const MCInst &MI, unsigned OpNo,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;
  uint64_t getLeftByteOpValue(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  uint64_t getShortBrOpValue(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  uint64_t getShortValOpValue(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  uint64_t getNormRegOpValue(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  uint64_t getNormExtOpValue(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

public:
  Mera400MCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : MCII(MCII), Ctx(Ctx) {}
  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};
} // namespace

// Registers encode as their number; immediates in the short fields are stored
// as sign and magnitude (bit 6 is the sign), not two's complement, so -1 is
// 0x41.
unsigned
Mera400MCCodeEmitter::getMachineOpValue(const MCInst &, const MCOperand &Op,
                                        SmallVectorImpl<MCFixup> &,
                                        const MCSubtargetInfo &) const {
  if (Op.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(Op.getReg());
  int64_t Value = Op.getImm();
  return Value < 0 ? 0x40 | static_cast<unsigned>(-Value)
                   : static_cast<unsigned>(Value);
}

// The count is 4 bits: a negative count is stored as 16 + count (shc rotates
// right, so a negative count rotates left).
uint64_t Mera400MCCodeEmitter::getShiftCountOpValue(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &,
    const MCSubtargetInfo &) const {
  int64_t Count = MI.getOperand(OpNo).getImm();
  return (Count < 0 ? 16 + Count : Count) & 0xf;
}

// "blc" (check left byte) takes its operand in the high octet of a 16-bit
// value but encodes it in the instruction's low byte.
uint64_t Mera400MCCodeEmitter::getLeftByteOpValue(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &,
    const MCSubtargetInfo &) const {
  return MI.getOperand(OpNo).getImm() >> 8;
}

// A "plain" value (difference of two labels) is a distance, so it is taken
// literally (WORD7); a label is PC-relative (PCREL7).
// A label operand becomes a PC-relative fixup applied to the whole
// instruction word; a literal is sign-magnitude encoded directly.
uint64_t Mera400MCCodeEmitter::getShortBrOpValue(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  const MCOperand &Op = MI.getOperand(OpNo);
  if (Op.isImm())
    return getMachineOpValue(MI, Op, Fixups, STI);
  const auto *Half = cast<Mera400HalfExpr>(Op.getExpr());
  if (Half->isPlain())
    Fixups.push_back(
        MCFixup::create(0, Half->getSubExpr(), Mera400::fixup_word7));
  else
    Fixups.push_back(MCFixup::create(0, Half->getSubExpr(),
                                     Mera400::fixup_pcrel7, /*PCRel=*/true));
  return 0;
}

// A label operand becomes an absolute fixup holding its word address (WORD7),
// which only fits the 7-bit field for addresses within 63 words of zero.
uint64_t Mera400MCCodeEmitter::getShortValOpValue(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  const MCOperand &Op = MI.getOperand(OpNo);
  if (Op.isImm())
    return getMachineOpValue(MI, Op, Fixups, STI);
  Fixups.push_back(MCFixup::create(
      0, cast<Mera400HalfExpr>(Op.getExpr())->getSubExpr(),
      Mera400::fixup_word7));
  return 0;
}

uint64_t Mera400MCCodeEmitter::getNormRegOpValue(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &,
    const MCSubtargetInfo &) const {
  return MI.getOperand(OpNo).getImm();
}

// The control field goes in bits 22-16 and the extra word in bits 15-0 of the
// returned value; a symbolic extra word is left to a fixup at byte offset 2.
uint64_t Mera400MCCodeEmitter::getNormExtOpValue(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &) const {
  uint64_t Control = MI.getOperand(OpNo).getImm();
  const MCOperand &Extra = MI.getOperand(OpNo + 1);
  uint64_t Word = 0;
  if (Extra.isImm())
    Word = static_cast<uint16_t>(Extra.getImm());
  else
    Fixups.push_back(MCFixup::create(2, Extra.getExpr(), FK_Data_2));
  return Control << 16 | Word;
}

// Writes one or two big-endian words. Every instruction also gets a fixup that
// checks it sits at an even byte offset.
void Mera400MCCodeEmitter::encodeInstruction(const MCInst &MI,
                                             SmallVectorImpl<char> &CB,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  Fixups.push_back(MCFixup::create(
      0,
      MCUnaryExpr::createPlus(MCConstantExpr::create(0, Ctx), Ctx, MI.getLoc()),
      Mera400::fixup_instruction_alignment));
  uint64_t Code = getBinaryCodeForInstr(MI, Fixups, STI);
  if (MCII.get(MI.getOpcode()).getSize() == 4)
    support::endian::write(CB, static_cast<uint32_t>(Code), endianness::big);
  else
    support::endian::write(CB, static_cast<uint16_t>(Code), endianness::big);
}

MCCodeEmitter *llvm::createMera400MCCodeEmitter(const MCInstrInfo &MCII,
                                                MCContext &Ctx) {
  return new Mera400MCCodeEmitter(MCII, Ctx);
}

#include "Mera400GenMCCodeEmitter.inc"
