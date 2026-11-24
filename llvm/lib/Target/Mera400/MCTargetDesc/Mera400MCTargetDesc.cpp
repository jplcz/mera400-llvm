//===-- Mera400MCTargetDesc.cpp - MERA-400 MC registration ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registers the MERA-400 machine code layer components (assembly info,
// instruction/register/subtarget tables, printer, emitter, backend and target
// streamers) with the target registry.
//
//===----------------------------------------------------------------------===//

#include "Mera400MCTargetDesc.h"
#include "Mera400InstPrinter.h"
#include "Mera400TargetStreamer.h"
#include "TargetInfo/Mera400TargetInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/LEB128.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "Mera400GenInstrInfo.inc"
#define GET_REGINFO_MC_DESC
#include "Mera400GenRegisterInfo.inc"
#define GET_SUBTARGETINFO_MC_DESC
#include "Mera400GenSubtargetInfo.inc"

std::string Mera400::getICFromR4Rule() {
  // IC = R4 << 1
  std::string Expr;
  Expr += char(dwarf::DW_OP_breg0 + DwarfR4);
  Expr += char(0);
  Expr += char(dwarf::DW_OP_lit1);
  Expr += char(dwarf::DW_OP_shl);
  std::string Rule;
  raw_string_ostream OS(Rule);
  OS << char(dwarf::DW_CFA_val_expression);
  encodeULEB128(DwarfIC, OS);
  encodeULEB128(Expr.size(), OS);
  OS << Expr;
  return Rule;
}

std::string Mera400::getICFromSavedR4Rule(int64_t CFAOffset) {
  // IC = (*(uint16_t *)(CFA + CFAOffset)) << 1
  std::string Expr;
  raw_string_ostream ExprOS(Expr);
  ExprOS << char(dwarf::DW_OP_call_frame_cfa) << char(dwarf::DW_OP_consts);
  encodeSLEB128(CFAOffset, ExprOS);
  ExprOS << char(dwarf::DW_OP_plus) << char(dwarf::DW_OP_deref_size)
         << char(2) << char(dwarf::DW_OP_lit1) << char(dwarf::DW_OP_shl);
  std::string Rule;
  raw_string_ostream OS(Rule);
  OS << char(dwarf::DW_CFA_val_expression);
  encodeULEB128(DwarfIC, OS);
  encodeULEB128(Expr.size(), OS);
  OS << Expr;
  return Rule;
}

namespace {
// Big-endian words, 2-byte instruction alignment, and ";" starts a comment.
class Mera400MCAsmInfo : public MCAsmInfoELF {
public:
  explicit Mera400MCAsmInfo(const MCTargetOptions &Options)
      : MCAsmInfoELF(Options) {
    IsLittleEndian = false;
    MinInstAlignment = 2;
    CommentString = ";";
    // EMAS has no statement separator; the default ";" is the comment
    // character here, so a character that never appears in sources is used.
    SeparatorString = "{";
    // Debug information is emitted only on request (-g). Offsets and
    // addresses use 4-byte ELF fields in both memory models, so DWARF needs no
    // 16-bit relocations. Stack slots are one word.
    SupportsDebugInformation = true;
    UsesCFIWithoutEH = true; // call frame information for -g without exceptions
    CodePointerSize = 4;
    CalleeSaveStackSlotSize = 2;
  }
};

// Lets tools such as llvm-objdump find branch targets. Addresses are ELF
// byte addresses; MERA-400 jump values are word addresses, hence the factor 2.
class Mera400MCInstrAnalysis : public MCInstrAnalysis {
public:
  using MCInstrAnalysis::MCInstrAnalysis;

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    switch (Inst.getOpcode()) {
    case Mera400::UJS:
    case Mera400::JLS:
    case Mera400::JES:
    case Mera400::JGS:
    case Mera400::JVS:
    case Mera400::JXS:
    case Mera400::JYS:
    case Mera400::JCS:
      return shortTarget(Inst.getOperand(0), Addr, Size, Target);
    case Mera400::IRB:
    case Mera400::DRB:
      return shortTarget(Inst.getOperand(1), Addr, Size, Target);
    case Mera400::UJ_EXT:
    case Mera400::JL_EXT:
    case Mera400::JE_EXT:
    case Mera400::JG_EXT:
    case Mera400::JZ_EXT:
    case Mera400::JM_EXT:
    case Mera400::JN_EXT:
      // Only a plain value: not indirect and without an index register.
      if (Inst.getOperand(0).getImm() != 0)
        return false;
      Target = static_cast<uint64_t>(Inst.getOperand(1).getImm() & 0xffff) * 2;
      return true;
    default:
      return false;
    }
  }

private:
  // A short displacement counts words from the next instruction.
  static bool shortTarget(const MCOperand &Op, uint64_t Addr, uint64_t Size,
                          uint64_t &Target) {
    if (!Op.isImm())
      return false;
    int64_t T = static_cast<int64_t>(Addr + Size) + Op.getImm() * 2;
    if (T < 0)
      return false;
    Target = T;
    return true;
  }
};

MCInstrAnalysis *createInstrAnalysis(const MCInstrInfo *Info) {
  return new Mera400MCInstrAnalysis(Info);
}

// Memory is scarce and there is no paging, so code is aligned to one word (the
// instruction size), not to the generic default of four bytes.
class Mera400MCObjectFileInfo : public MCObjectFileInfo {
public:
  unsigned getTextSectionAlignment() const override { return 2; }
};

MCObjectFileInfo *createObjectFileInfo(MCContext &Ctx, bool PIC,
                                       bool LargeCodeModel) {
  auto *MOFI = new Mera400MCObjectFileInfo();
  MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
  return MOFI;
}

MCAsmInfo *createAsmInfo(const MCRegisterInfo &, const Triple &,
                         const MCTargetOptions &Options) {
  auto *MAI = new Mera400MCAsmInfo(Options);
  // At function entry the CFA (the stack pointer before the call) is R7, and
  // the caller's IC is derived from R4. The stub reports R7 in bytes.
  MAI->addInitialFrameState(MCCFIInstruction::cfiDefCfa(
      nullptr, Mera400::DwarfR7, 0));
  MAI->addInitialFrameState(
      MCCFIInstruction::createEscape(nullptr, Mera400::getICFromR4Rule()));
  return MAI;
}

MCInstrInfo *createInstrInfo() {
  auto *Info = new MCInstrInfo();
  InitMera400MCInstrInfo(Info);
  return Info;
}

MCRegisterInfo *createRegisterInfo(const Triple &) {
  auto *Info = new MCRegisterInfo();
  InitMera400MCRegisterInfo(Info, Mera400::IC);
  return Info;
}

MCSubtargetInfo *createSubtargetInfo(const Triple &TT, StringRef CPU,
                                     StringRef FS) {
  return createMera400MCSubtargetInfoImpl(TT, CPU, CPU, FS);
}

MCInstPrinter *createInstPrinter(const Triple &, unsigned Variant,
                                 const MCAsmInfo &MAI, const MCInstrInfo &MII,
                                 const MCRegisterInfo &MRI) {
  return Variant == 0 ? new Mera400InstPrinter(MAI, MII, MRI) : nullptr;
}
} // namespace

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMera400TargetMC() {
  Target &T = getTheMera400Target();
  TargetRegistry::RegisterMCAsmInfo(T, createAsmInfo);
  TargetRegistry::RegisterMCObjectFileInfo(T, createObjectFileInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createInstPrinter);
  TargetRegistry::RegisterMCInstrAnalysis(T, createInstrAnalysis);
  TargetRegistry::RegisterMCCodeEmitter(T, createMera400MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createMera400MCAsmBackend);
  TargetRegistry::RegisterNullTargetStreamer(T, createMera400NullTargetStreamer);
  TargetRegistry::RegisterAsmTargetStreamer(T, createMera400AsmTargetStreamer);
  TargetRegistry::RegisterObjectTargetStreamer(
      T, createMera400ObjectTargetStreamer);
}
