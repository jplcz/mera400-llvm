//===-- Mera400MCTargetDesc.h - MERA-400 MC interfaces --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the MC (machine code layer) components of the MERA-400 target: the
// fixup kinds used by the assembler, and the factories for the code emitter,
// assembler backend and target streamers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MCTARGETDESC_MERA400MCTARGETDESC_H
#define LLVM_LIB_TARGET_MERA400_MCTARGETDESC_MERA400MCTARGETDESC_H

#include "llvm/MC/MCFixup.h"
#include <cstdint>
#include <string>

namespace llvm {
class Target;
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;

namespace Mera400 {
enum Fixups {
  fixup_instruction_alignment = FirstTargetFixupKind,
  // 7-bit sign-magnitude word offset of a short branch, relative to the
  // instruction after the branch.
  fixup_pcrel7,
  // 7-bit sign-magnitude word address of a symbol (short value operands).
  fixup_word7,
};

// DWARF numbers of the registers debuggers see (they equal the hardware
// numbers): R4 receives the return address and R7 is the stack pointer. IC,
// the instruction counter, is the DWARF return address column.
enum DwarfRegisters : unsigned { DwarfR4 = 4, DwarfR7 = 7, DwarfIC = 8 };

// Raw DWARF call frame instructions (DW_CFA_val_expression) that compute the
// caller's IC. R4 and return addresses saved in the frame are *word*
// addresses, while debuggers use byte addresses, so both rules multiply by 2.
//
// A leaf function still holds its return address in R4.
std::string getICFromR4Rule();
// A function that makes calls saved R4 in the frame, CFAOffset bytes from the
// CFA (a negative number).
std::string getICFromSavedR4Rule(int64_t CFAOffset);
} // namespace Mera400

MCCodeEmitter *createMera400MCCodeEmitter(const MCInstrInfo &MCII,
                                          MCContext &Ctx);
MCAsmBackend *createMera400MCAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &Options);
} // namespace llvm

#define GET_REGINFO_ENUM
#include "Mera400GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "Mera400GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "Mera400GenSubtargetInfo.inc"

#endif
