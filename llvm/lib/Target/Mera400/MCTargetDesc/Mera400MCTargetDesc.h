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
}

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
