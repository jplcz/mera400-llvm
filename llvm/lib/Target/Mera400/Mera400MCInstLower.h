//===-- Mera400MCInstLower.h - Lower MachineInstr to MCInst -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Converts code generation instructions (MachineInstr) into MC (machine code
// layer) instructions for printing and encoding.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400MCINSTLOWER_H
#define LLVM_LIB_TARGET_MERA400_MERA400MCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class AsmPrinter;
class MCContext;
class MCInst;
class MachineInstr;
class MachineOperand;
class MCOperand;
template <typename T> class SmallVectorImpl;

// Turns the code generation pseudo instructions into the real instructions
// that the assembler, encoder and disassembler know.
class LLVM_LIBRARY_VISIBILITY Mera400MCInstLower {
  AsmPrinter &AP;

  MCOperand lowerExtra(const MachineOperand &MO) const;

public:
  explicit Mera400MCInstLower(AsmPrinter &AP) : AP(AP) {}

  // Most pseudos become one instruction; a byte load needs two.
  void lower(const MachineInstr *MI, SmallVectorImpl<MCInst> &Out) const;
};

} // namespace llvm

#endif
