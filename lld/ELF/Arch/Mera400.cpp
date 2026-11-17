//===- Mera400.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MERA-400 is a 16-bit word-addressed minicomputer with a 65536-word
// address space. Programs are loaded as flat memory images, so there is no
// dynamic linking, no PLT/GOT and no position independent code: every address
// is fixed at link time and the image starts at address 0.
//
// ELF addresses and symbol values are bytes; machine addresses are words, so
// relocations that store an address divide it by two. Objects use REL
// relocations: the in-place field is the addend.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class Mera400 final : public TargetInfo {
public:
  Mera400(Ctx &);
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

Mera400::Mera400(Ctx &ctx) : TargetInfo(ctx) {
  // The machine's memory starts at word 0 and is not paged.
  defaultImageBase = 0;
  defaultMaxPageSize = 2;
  defaultCommonPageSize = 2;
  symbolicRel = R_MERA400_16;
  // hlt 0, so stray execution of padding stops the processor.
  trapInstr = {0xec, 0x00, 0xec, 0x00};
}

RelExpr Mera400::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  switch (type) {
  case R_MERA400_NONE:
    return R_NONE;
  case R_MERA400_16:
  case R_MERA400_WORD16:
  case R_MERA400_32:
  case R_MERA400_HI16:
  case R_MERA400_LO16:
    return R_ABS;
  case R_MERA400_WORD7:
    return R_ABS;
  case R_MERA400_PCREL7:
    return R_PC;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation " << type << " against symbol "
             << &s;
    return R_NONE;
  }
}

// The in-place value of a WORD16 relocation is a word count, so it is turned
// back into bytes to be added to the symbol address. The 7-bit fields hold a
// sign-and-magnitude word count.
int64_t Mera400::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_MERA400_16:
    return read16be(buf);
  case R_MERA400_WORD16:
    return int64_t(read16be(buf)) * 2;
  case R_MERA400_32:
    return int32_t(read32be(buf));
  case R_MERA400_HI16:
  case R_MERA400_LO16:
    return int16_t(read16be(buf));
  case R_MERA400_PCREL7:
  case R_MERA400_WORD7: {
    uint16_t w = read16be(buf);
    int64_t mag = w & 0x3f;
    return (w & 0x200 ? -mag : mag) * 2;
  }
  default:
    return 0;
  }
}

void Mera400::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  switch (rel.type) {
  case R_MERA400_16:
    checkUInt(ctx, loc, val, 16, rel);
    write16be(loc, val);
    break;
  case R_MERA400_WORD16:
    checkAlignment(ctx, loc, val, 2, rel);
    checkUInt(ctx, loc, val >> 1, 16, rel);
    write16be(loc, val >> 1);
    break;
  case R_MERA400_32:
  case R_MERA400_HI16:
  case R_MERA400_LO16:
    // A byte address of the 128 KiB memory.
    checkUInt(ctx, loc, val, 17, rel);
    if (rel.type == R_MERA400_32)
      write32be(loc, val);
    else
      write16be(loc, rel.type == R_MERA400_HI16 ? val >> 16 : val);
    break;
  case R_MERA400_WORD7: {
    checkAlignment(ctx, loc, val, 2, rel);
    int64_t words = int64_t(val) / 2;
    if (words < -63 || words > 63)
      reportRangeError(ctx, loc, rel, Twine(words), -63, 63);
    uint16_t field = words < 0 ? 0x200 | uint16_t(-words) : uint16_t(words);
    write16be(loc, (read16be(loc) & ~0x23f) | field);
    break;
  }
  case R_MERA400_PCREL7: {
    // Displacement in words from the next instruction, stored as sign
    // (bit 9) and magnitude (bits 5-0) inside the instruction word.
    checkAlignment(ctx, loc, val, 2, rel);
    int64_t words = int64_t(val) / 2 - 1;
    if (words < -63 || words > 63)
      reportRangeError(ctx, loc, rel, Twine(words), -63, 63);
    uint16_t field = words < 0 ? 0x200 | uint16_t(-words) : uint16_t(words);
    write16be(loc, (read16be(loc) & ~0x23f) | field);
    break;
  }
  default:
    llvm_unreachable("unknown relocation");
  }
}

static uint32_t getEFlags(ELFFileBase *file) {
  return cast<ObjFile<ELF32BE>>(file)->getObj().getHeader().e_flags;
}

// An object compiled for a model carries EF_MERA400_LARGE or
// EF_MERA400_SMALL; plain assembler objects carry neither and fit either.
uint32_t Mera400::calcEFlags() const {
  uint32_t flags = 0;
  ELFFileBase *large = nullptr, *small = nullptr;
  for (ELFFileBase *f : ctx.objectFiles) {
    uint32_t objFlags = getEFlags(f);
    if ((objFlags & EF_MERA400_LARGE) && !large)
      large = f;
    if ((objFlags & EF_MERA400_SMALL) && !small)
      small = f;
    flags |= objFlags & (EF_MERA400_LARGE | EF_MERA400_SMALL);
  }
  if (large && small)
    ErrAlways(ctx) << "cannot link the large memory model object " << large
                   << " with the small memory model object " << small;
  return flags;
}

void elf::setMera400TargetInfo(Ctx &ctx) { ctx.target.reset(new Mera400(ctx)); }
