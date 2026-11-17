//===-- Mera400InstPrinter.cpp - MERA-400 assembly printer ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Prints MC instructions as MERA-400 assembly, including register names,
// short branch offsets and the extra-word operand forms.
//
//===----------------------------------------------------------------------===//

#include "Mera400InstPrinter.h"
#include "Mera400MCExpr.h"
#include "Mera400MCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#include "Mera400GenAsmWriter.inc"

void Mera400InstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  OS << getRegisterName(Reg);
}

void Mera400InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                   StringRef Annot, const MCSubtargetInfo &STI,
                                   raw_ostream &OS) {
  printInstruction(MI, Address, OS);
  printAnnotation(OS, Annot);
}

// A word-valued operand holds a byte-valued expression; the assembler syntax
// writes the symbol and an offset in words ("sym+3"). The hi() and lo() forms
// take their argument in bytes, as in data directives.
static void printWordExpr(raw_ostream &OS, const MCAsmInfo &MAI,
                          const Mera400HalfExpr *Half) {
  if (Half->getPart() != Mera400HalfExpr::Words)
    return MAI.printExpr(OS, *Half);
  const MCExpr *Sub = Half->getSubExpr();
  if (const auto *Bin = dyn_cast<MCBinaryExpr>(Sub)) {
    const auto *Off = dyn_cast<MCConstantExpr>(Bin->getRHS());
    if (Bin->getOpcode() == MCBinaryExpr::Add && Off &&
        Off->getValue() % 2 == 0) {
      MAI.printExpr(OS, *Bin->getLHS());
      int64_t Words = Off->getValue() / 2;
      if (Words < 0)
        OS << '-' << -Words;
      else if (Words > 0)
        OS << '+' << Words;
      return;
    }
  }
  MAI.printExpr(OS, *Sub);
}

void Mera400InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg())
    printRegName(OS, Op.getReg());
  else
    OS << Op.getImm();
}

void Mera400InstPrinter::printShortBrOperand(const MCInst *MI, unsigned OpNo,
                                             raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm())
    OS << Op.getImm();
  else if (const auto *Half = dyn_cast<Mera400HalfExpr>(Op.getExpr()))
    MAI.printExpr(OS, *Half->getSubExpr());
  else
    MAI.printExpr(OS, *Op.getExpr());
}

// Prints the control field (bit 6 indirect, bits 5-3 register B, bits 2-0
// register C) and, for the two-word form, the extra word that follows it.
void Mera400InstPrinter::printNormOperand(const MCInst *MI, unsigned OpNo,
                                          raw_ostream &OS) {
  unsigned Control = MI->getOperand(OpNo).getImm();
  unsigned RegC = Control & 7, RegB = (Control >> 3) & 7;
  bool HasExtra = OpNo + 1 < MI->getNumOperands();
  if (Control & 0x40)
    OS << '[';
  if (HasExtra) {
    const MCOperand &Extra = MI->getOperand(OpNo + 1);
    if (Extra.isImm())
      OS << static_cast<int16_t>(Extra.getImm());
    else if (const auto *Half = dyn_cast<Mera400HalfExpr>(Extra.getExpr()))
      printWordExpr(OS, MAI, Half);
    else
      MAI.printExpr(OS, *Extra.getExpr());
    if (RegB)
      OS << '+' << getRegisterName(MCRegister(Mera400::R0 + RegB));
  } else {
    OS << getRegisterName(MCRegister(Mera400::R0 + RegC));
    if (RegB)
      OS << '+' << getRegisterName(MCRegister(Mera400::R0 + RegB));
  }
  if (Control & 0x40)
    OS << ']';
}
