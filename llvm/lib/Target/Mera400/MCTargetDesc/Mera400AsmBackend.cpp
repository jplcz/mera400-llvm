//===-- Mera400AsmBackend.cpp - MERA-400 ELF assembly backend -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines how the assembler resolves fixups (places in an instruction or data
// whose value is only known later) and how unresolved ones become ELF
// (Executable and Linkable Format) relocations. Objects are big-endian 16-bit
// words, and ELF symbol values are bytes, so address fixups often divide by 2.
//
// Relocations (S = symbol address in bytes, A = addend):
//   R_MERA400_16      S + A as 16 bits (a byte address in data)
//   R_MERA400_WORD16  (S + A) / 2 as 16 bits (a word address in an extra word)
//   R_MERA400_PCREL7  word distance to a label, in a short branch's 7-bit
//                     sign-magnitude field
//   R_MERA400_WORD7   (S + A) / 2 in a short instruction's 7-bit field
//   R_MERA400_32      S + A as a 32-bit byte address (large model data)
//   R_MERA400_HI16/LO16  high and low word of a 32-bit byte address
//
//===----------------------------------------------------------------------===//

#include "Mera400MCExpr.h"
#include "Mera400MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
// Chooses the relocation type for an unresolved fixup. 16-bit data fixups are
// told apart by the specifier that Mera400HalfExpr attached: a byte address, a
// word address, or one half of a 32-bit address.
class Mera400ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  Mera400ELFObjectWriter()
      : MCELFObjectTargetWriter(false, ELF::ELFOSABI_NONE,
                                ELF::EM_MERA400_EXPERIMENTAL, false) {}

  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool) const override {
    if (Fixup.getKind() == Mera400::fixup_pcrel7)
      return ELF::R_MERA400_PCREL7;
    if (Fixup.getKind() == Mera400::fixup_word7)
      return ELF::R_MERA400_WORD7;
    if (Fixup.getKind() == FK_Data_4)
      return ELF::R_MERA400_32;
    if (Fixup.getKind() == FK_Data_2) {
      switch (Target.getSpecifier()) {
      case Mera400HalfExpr::VK_WORD16:
        return ELF::R_MERA400_WORD16;
      case Mera400HalfExpr::VK_HI16:
        return ELF::R_MERA400_HI16;
      case Mera400HalfExpr::VK_LO16:
        return ELF::R_MERA400_LO16;
      default:
        return ELF::R_MERA400_16;
      }
    }
    reportError(Fixup.getLoc(), "unsupported MERA-400 relocation");
    return 0;
  }

  // The 16-bit addend of HI16 and LO16 is the offset from the symbol itself
  // (not from its section), so that it stays small.
  bool needsRelocateWithSymbol(const MCValue &,
                               unsigned Type) const override {
    return Type == ELF::R_MERA400_HI16 || Type == ELF::R_MERA400_LO16;
  }
};

// Instructions are 16-bit big-endian words (high octet first, as on the
// machine), so the backend is big-endian even though the ELF file is only a
// transport format on the PC.
class Mera400AsmBackend : public MCAsmBackend {
public:
  Mera400AsmBackend() : MCAsmBackend(endianness::big) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return std::make_unique<Mera400ELFObjectWriter>();
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    if (Kind == Mera400::fixup_instruction_alignment)
      return {"fixup_instruction_alignment", 0, 0, 0};
    if (Kind == Mera400::fixup_pcrel7)
      return {"fixup_pcrel7", 0, 16, 0};
    if (Kind == Mera400::fixup_word7)
      return {"fixup_word7", 0, 16, 0};
    return MCAsmBackend::getFixupKindInfo(Kind);
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    if (Fixup.getKind() == Mera400::fixup_instruction_alignment) {
      // A zero-sized fixup the code emitter attaches to every instruction so
      // that a misplaced (odd-offset) instruction is diagnosed.
      if ((Asm->getFragmentOffset(F) + Fixup.getOffset()) % 2)
        getContext().reportError(Fixup.getLoc(),
                                 "instruction must be 2-byte aligned");
      return;
    }

    maybeAddReloc(F, Fixup, Target, Value, IsResolved);
    // Short branch field: sign (bit 9) and magnitude (bits 0..5) of a word
    // count, the same encoding as the short immediates.
    if (Fixup.getKind() == Mera400::fixup_pcrel7) {
      if (!IsResolved) {
        // REL relocations keep the addend in place: it is a word count in the
        // 7-bit field, and the linker adds the symbol's displacement to it.
        int64_t Addend = static_cast<int64_t>(Value);
        if (Addend % 2 || Addend / 2 < -63 || Addend / 2 > 63) {
          getContext().reportError(Fixup.getLoc(),
                                   "short branch offset is out of range "
                                   "[-63, 63] words or not word aligned");
          return;
        }
        int64_t Words = Addend / 2;
        uint16_t Bits = (Words < 0 ? 0x200 : 0) | (std::abs(Words) & 0x3f);
        Data[0] |= Bits >> 8;
        Data[1] |= Bits & 0xff;
        return;
      }
      // The offset counts words from the next instruction.
      int64_t Words = static_cast<int64_t>(Value) / 2 - 1;
      if (Value % 2 || Words < -63 || Words > 63) {
        getContext().reportError(Fixup.getLoc(),
                                 "short branch target is out of range "
                                 "[-63, 63] words or not word aligned");
        return;
      }
      uint16_t Bits = (Words < 0 ? 0x200 : 0) | (std::abs(Words) & 0x3f);
      Data[0] |= Bits >> 8;
      Data[1] |= Bits & 0xff;
      return;
    }
    if (Fixup.getKind() == Mera400::fixup_word7) {
      // For an unresolved fixup Value is the addend, stored in words.
      int64_t Bytes = static_cast<int64_t>(Value);
      int64_t Words = Bytes / 2;
      if (Bytes % 2 || Words < -63 || Words > 63) {
        getContext().reportError(Fixup.getLoc(),
                                 "short value is out of range [-63, 63] words "
                                 "or not word aligned");
        return;
      }
      uint16_t Bits = (Words < 0 ? 0x200 : 0) | (std::abs(Words) & 0x3f);
      Data[0] |= Bits >> 8;
      Data[1] |= Bits & 0xff;
      return;
    }
    // A word address: ELF values are byte addresses, so halve it.
    if (Target.getSpecifier() == Mera400HalfExpr::VK_WORD16)
      Value = static_cast<int64_t>(Value) / 2;
    MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
    unsigned Bytes = (Info.TargetSize + 7) / 8;
    for (unsigned I = 0; I < Bytes; ++I)
      Data[I] = static_cast<uint8_t>(Value >> (8 * (Bytes - I - 1)));
  }

  // Padding is whole words ("ujs 0", the machine's nop). An odd section offset
  // can require a single zero byte before the first nop.
  unsigned getMinimumNopSize() const override { return 1; }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *) const override {
    if (Count & 1) {
      OS << '\0';
      --Count;
    }
    // EMAS NOP is UJS 0.
    while (Count) {
      OS.write("\xe0\x00", 2);
      Count -= 2;
    }
    return true;
  }
};
} // namespace

MCAsmBackend *llvm::createMera400MCAsmBackend(const Target &,
                                              const MCSubtargetInfo &,
                                              const MCRegisterInfo &,
                                              const MCTargetOptions &) {
  return new Mera400AsmBackend();
}
