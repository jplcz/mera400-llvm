//===-- Mera400MCExpr.cpp - MERA-400 target expressions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Evaluates the word/byte conversion expression for the assembler. When the
// operand is a plain number the conversion is done immediately; when it names
// a symbol the final address is unknown, so a relocation (WORD16, HI16 or
// LO16) is requested and the linker divides or splits the address later.
//
//===----------------------------------------------------------------------===//

#include "Mera400MCExpr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

const Mera400HalfExpr *Mera400HalfExpr::create(const MCExpr *Sub,
                                               MCContext &Ctx, bool Plain,
                                               Part P) {
  return new (Ctx) Mera400HalfExpr(Sub, Plain, P);
}

void Mera400HalfExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  OS << (P == Hi ? "hi(" : P == Lo ? "lo(" : "words(");
  MAI->printExpr(OS, *Sub);
  OS << ')';
}

bool Mera400HalfExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                                const MCAssembler *Asm) const {
  MCValue V;
  if (!Sub->evaluateAsRelocatable(V, Asm) || V.getSubSym() ||
      V.getSpecifier() != VK_None)
    return false;
  if (P != Words) {
    // The relocation carries a 16-bit signed addend (the offset from the
    // symbol), so the part of a symbol plus offset is taken by the linker.
    if (V.isAbsolute()) {
      uint64_t Bytes = V.getConstant();
      Res = MCValue::get(P == Hi ? (Bytes >> 16) & 0xffff : Bytes & 0xffff);
      return true;
    }
    if (V.getConstant() < -32768 || V.getConstant() > 32767)
      return false;
    Res = MCValue::get(V.getAddSym(), nullptr, V.getConstant(),
                       P == Hi ? VK_HI16 : VK_LO16);
    return true;
  }
  if (V.isAbsolute()) {
    Res = MCValue::get(V.getConstant() / 2);
    return true;
  }
  // A relocation can only halve a symbol plus an even offset.
  if (V.getConstant() % 2)
    return false;
  Res = MCValue::get(V.getAddSym(), nullptr, V.getConstant(), VK_WORD16);
  return true;
}

void Mera400HalfExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*Sub);
}

MCFragment *Mera400HalfExpr::findAssociatedFragment() const {
  return Sub->findAssociatedFragment();
}
