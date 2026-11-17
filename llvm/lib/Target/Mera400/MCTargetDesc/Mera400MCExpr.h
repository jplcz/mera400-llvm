//===-- Mera400MCExpr.h - MERA-400 target expressions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A target expression that converts between ELF byte values and MERA-400 word
// values, and selects the high or low half of a 32-bit byte address.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MCTARGETDESC_MERA400MCEXPR_H
#define LLVM_LIB_TARGET_MERA400_MCTARGETDESC_MERA400MCEXPR_H

#include "llvm/MC/MCExpr.h"

namespace llvm {

// The MERA-400 addresses memory in 16-bit words while ELF symbols are in
// bytes. This expression converts a byte-valued expression to words by
// halving it. Evaluating `sym + C` stays relocatable: the result carries
// the WORD16 specifier so the object writer emits R_MERA400_WORD16.
//
// The large memory model needs the byte address itself, as two words: the
// Hi and Lo parts (written "hi(expr)" and "lo(expr)") select the high or low
// 16 bits of the byte-valued expression and become R_MERA400_HI16 and
// R_MERA400_LO16.
class Mera400HalfExpr : public MCTargetExpr {
public:
  enum Specifier : uint32_t {
    VK_None = 0,
    VK_WORD16 = 1,
    VK_HI16 = 2,
    VK_LO16 = 3
  };
  enum Part { Words, Hi, Lo };

private:
  const MCExpr *Sub;
  bool Plain;
  Part P;
  Mera400HalfExpr(const MCExpr *Sub, bool Plain, Part P)
      : Sub(Sub), Plain(Plain), P(P) {}

public:
  // A plain value is a difference of two addresses: a distance, not an
  // address, so short branches take it literally instead of making it
  // relative to the instruction.
  static const Mera400HalfExpr *create(const MCExpr *Sub, MCContext &Ctx,
                                       bool Plain = false, Part P = Words);
  const MCExpr *getSubExpr() const { return Sub; }
  bool isPlain() const { return Plain; }
  Part getPart() const { return P; }

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res,
                                 const MCAssembler *Asm) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override;

  // This is the only target expression the MERA-400 backend creates.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};

} // namespace llvm

#endif
