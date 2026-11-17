//===-- Mera400MCInstLower.cpp - Lower MachineInstr to MCInst
//----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Code generation pseudo instructions (Mera400InstrInfo.td) have explicit
// outputs and are easy for the register allocator, but are not what the
// hardware runs. When assembly is printed, each is turned here into one or
// more real MC (machine code layer) instructions defined in Mera400.td. The
// multi-instruction pseudos exist so that carry and compare flags in R0 are
// produced and consumed inside one expansion, never across pseudos.
//
//===----------------------------------------------------------------------===//

#include "Mera400MCInstLower.h"
#include "Mera400.h"
#include "MCTargetDesc/Mera400MCExpr.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

// The packed "control" operand of a real instruction names register C when it
// is the only field set (D = 0, B = 0), so it is the register's number.
static unsigned registerControl(Register Reg) { return Reg.id() - Mera400::R0; }

static void addReg(MCInst &Out, Register Reg) {
  Out.addOperand(MCOperand::createReg(Reg.asMCReg()));
}

// The extra word of an instruction: a constant, or a symbol the linker
// resolves with a 16-bit word relocation (WORD16: the symbol's byte address in
// the ELF file divided by two).
MCOperand Mera400MCInstLower::lowerExtra(const MachineOperand &MO) const {
  MCContext &Ctx = AP.OutContext;
  const MCSymbol *Sym;
  int64_t Offset = 0;
  switch (MO.getType()) {
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(static_cast<int16_t>(MO.getImm()));
  case MachineOperand::MO_GlobalAddress:
    Sym = AP.getSymbol(MO.getGlobal());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_ExternalSymbol:
    Sym = AP.GetExternalSymbolSymbol(MO.getSymbolName());
    break;
  default:
    report_fatal_error("Mera400: unsupported operand in an address");
  }
  // Symbols are byte addresses in the object file but operands are word
  // addresses, so the symbol is wrapped in a "half" expression (see
  // Mera400MCExpr). A global's offset is counted in words, hence the doubling
  // to bytes before it is added.
  const MCExpr *E = MCSymbolRefExpr::create(Sym, Ctx);
  if (Offset)
    E = MCBinaryExpr::createAdd(E, MCConstantExpr::create(Offset * 2, Ctx),
                                Ctx);
  return MCOperand::createExpr(Mera400HalfExpr::create(E, Ctx));
}

// The extra word of a long jump holds the word address of a block.
static MCOperand blockAddress(const MachineOperand &MO, MCContext &Ctx) {
  return MCOperand::createExpr(Mera400HalfExpr::create(
      MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx), Ctx));
}

// "uj", "jl", "je", "jg" and "jn" (two words) or, when the destination is
// within reach, "ujs", "jls", "jes" and "jgs" (one word, relative).
enum JumpKind { JK_UJ, JK_JL, JK_JE, JK_JG, JK_JN };

static bool branchIsShort(JumpKind Kind) {
  return Mera400::UseShortJumps && Kind != JK_JN;
}

static void lowerJump(SmallVectorImpl<MCInst> &Insts, JumpKind Kind,
                      bool Short, const MachineOperand &Dst, MCContext &Ctx) {
  static const unsigned Long[] = {Mera400::UJ_EXT, Mera400::JL_EXT,
                                  Mera400::JE_EXT, Mera400::JG_EXT,
                                  Mera400::JN_EXT};
  static const unsigned Near[] = {Mera400::UJS, Mera400::JLS, Mera400::JES,
                                  Mera400::JGS};
  MCInst &Out = Insts.emplace_back();
  if (Short) {
    assert(Kind != JK_JN && "there is no short jump if not equal");
    Out.setOpcode(Near[Kind]);
    Out.addOperand(blockAddress(Dst, Ctx));
    return;
  }
  Out.setOpcode(Long[Kind]);
  Out.addOperand(MCOperand::createImm(0));
  Out.addOperand(blockAddress(Dst, Ctx));
}

// The jumps that take the branch for a condition after the compare has set
// one of the L, E and G flags (and only one: cw and cl leave the other flags
// alone). There is no jump for "less or equal" or "greater or equal", so two
// jumps are used: "jl" then "je", or "jg" then "je". "jn" tests that E is
// clear.
static unsigned jumpsFor(Mera400::CondCode CC, JumpKind (&Kinds)[2]) {
  switch (CC) {
  case Mera400::CC_EQ:
    Kinds[0] = JK_JE;
    return 1;
  case Mera400::CC_NE:
    Kinds[0] = JK_JN;
    return 1;
  case Mera400::CC_LT:
  case Mera400::CC_ULT:
    Kinds[0] = JK_JL;
    return 1;
  case Mera400::CC_GT:
  case Mera400::CC_UGT:
    Kinds[0] = JK_JG;
    return 1;
  case Mera400::CC_LE:
  case Mera400::CC_ULE:
    Kinds[0] = JK_JL;
    Kinds[1] = JK_JE;
    return 2;
  case Mera400::CC_GE:
  case Mera400::CC_UGE:
    Kinds[0] = JK_JG;
    Kinds[1] = JK_JE;
    return 2;
  }
  llvm_unreachable("invalid condition code");
}

// Register-register form: "op rA, rC" is one instruction word.
static void lowerRegArg(MCInst &Out, unsigned Opcode, Register Dst,
                        Register Src) {
  Out.setOpcode(Opcode);
  addReg(Out, Dst);
  Out.addOperand(MCOperand::createImm(registerControl(Src)));
}

// Immediate form: control field 0 selects the extra 16-bit word that holds
// the value.
static void lowerExtArg(MCInst &Out, unsigned Opcode, Register Dst,
                        MCOperand Extra) {
  Out.setOpcode(Opcode);
  addReg(Out, Dst);
  Out.addOperand(MCOperand::createImm(0));
  Out.addOperand(Extra);
}
static void lowerExtArg(MCInst &Out, unsigned Opcode, Register Dst,
                        int64_t Imm) {
  lowerExtArg(Out, Opcode, Dst, MCOperand::createImm(static_cast<int16_t>(Imm)));
}

// "lwt" and "awt" hold a small value in the instruction word itself.
static void lowerShortOrExtArg(MCInst &Out, unsigned ShortOpcode,
                               unsigned ExtOpcode, Register Dst, int64_t Imm) {
  Imm = static_cast<int16_t>(Imm);
  if (!Mera400::isShortImm(Imm)) {
    lowerExtArg(Out, ExtOpcode, Dst, Imm);
    return;
  }
  Out.setOpcode(ShortOpcode);
  addReg(Out, Dst);
  Out.addOperand(MCOperand::createImm(Imm));
}

// "op rA, [base+]disp": the plain register form when there is no displacement,
// otherwise the extra word holds the displacement and the B field the base.
static void lowerMemArg(MCInst &Out, unsigned RegOpcode, unsigned ExtOpcode,
                        Register Reg, Register Base, MCOperand Disp) {
  if (Base && Disp.isImm() && Disp.getImm() == 0) {
    lowerRegArg(Out, RegOpcode, Reg, Base);
    return;
  }
  Out.setOpcode(ExtOpcode);
  addReg(Out, Reg);
  Out.addOperand(
      MCOperand::createImm(Base ? (registerControl(Base) << 3) : 0));
  Out.addOperand(Disp);
}

// Same as lowerMemArg for the instructions that have no register operand (the
// AWP instructions "af", "sf", "mf" and "df", whose other operand is fixed).
static void lowerNoRegMemArg(MCInst &Out, unsigned RegOpcode,
                             unsigned ExtOpcode, Register Base,
                             MCOperand Disp) {
  if (Base && Disp.isImm() && Disp.getImm() == 0) {
    Out.setOpcode(RegOpcode);
    Out.addOperand(MCOperand::createImm(registerControl(Base)));
    return;
  }
  Out.setOpcode(ExtOpcode);
  Out.addOperand(
      MCOperand::createImm(Base ? (registerControl(Base) << 3) : 0));
  Out.addOperand(Disp);
}

// Helpers for pseudos that expand to several instructions.
static void emitReg(SmallVectorImpl<MCInst> &Insts, unsigned Opcode,
                    Register Dst, Register Src) {
  lowerRegArg(Insts.emplace_back(), Opcode, Dst, Src);
}

static void emitShort(SmallVectorImpl<MCInst> &Insts, unsigned Opcode,
                      Register Dst, int64_t Imm) {
  MCInst &Out = Insts.emplace_back();
  Out.setOpcode(Opcode);
  addReg(Out, Dst);
  Out.addOperand(MCOperand::createImm(Imm));
}

static void emitUnary(SmallVectorImpl<MCInst> &Insts, unsigned Opcode,
                      Register Dst) {
  MCInst &Out = Insts.emplace_back();
  Out.setOpcode(Opcode);
  addReg(Out, Dst);
}

// Lowers one pseudo into Insts. Operand 0 of the MachineInstr is usually the
// destination; Out is the first real instruction, and pseudos that expand to a
// sequence discard it (pop_back) and use the emit* helpers instead.
void Mera400MCInstLower::lower(const MachineInstr *MI,
                               SmallVectorImpl<MCInst> &Insts) const {
  MCInst &Out = Insts.emplace_back();
  auto Reg = [&](unsigned Idx) { return MI->getOperand(Idx).getReg(); };
  auto Imm = [&](unsigned Idx) { return MI->getOperand(Idx).getImm(); };
  auto Extra = [&](unsigned Idx) { return lowerExtra(MI->getOperand(Idx)); };

  // Operand 0 is the destination; for two-address pseudos operand 1 is tied
  // to it, so the real instruction names the destination only once.
  switch (MI->getOpcode()) {
  case Mera400::ADDrr:
    return lowerRegArg(Out, Mera400::AW, Reg(0), Reg(2));
  case Mera400::SUBrr:
    return lowerRegArg(Out, Mera400::SW, Reg(0), Reg(2));
  case Mera400::ANDrr:
    return lowerRegArg(Out, Mera400::NR, Reg(0), Reg(2));
  case Mera400::ORrr:
    return lowerRegArg(Out, Mera400::OR, Reg(0), Reg(2));
  case Mera400::XORrr:
    return lowerRegArg(Out, Mera400::XR, Reg(0), Reg(2));
  case Mera400::ADDri:
    return lowerShortOrExtArg(Out, Mera400::AWT, Mera400::AW_EXT, Reg(0),
                              Imm(2));
  case Mera400::ANDri:
    return lowerExtArg(Out, Mera400::NR_EXT, Reg(0), Imm(2));
  case Mera400::ORri:
    return lowerExtArg(Out, Mera400::OR_EXT, Reg(0), Imm(2));
  case Mera400::XORri:
    return lowerExtArg(Out, Mera400::XR_EXT, Reg(0), Imm(2));
  case Mera400::MOVri:
    if (!MI->getOperand(1).isImm())
      return lowerExtArg(Out, Mera400::LW_EXT, Reg(0), Extra(1));
    return lowerShortOrExtArg(Out, Mera400::LWT, Mera400::LW_EXT, Reg(0),
                              Imm(1));
  case Mera400::MOVrr:
    return lowerRegArg(Out, Mera400::LW, Reg(0), Reg(1));
  case Mera400::LDWrm:
    return lowerMemArg(Out, Mera400::TW, Mera400::TW_EXT, Reg(0), Reg(1),
                       Extra(2));
  case Mera400::LDWabs:
    return lowerExtArg(Out, Mera400::TW_EXT, Reg(0), Extra(1));
  case Mera400::STWrm:
    return lowerMemArg(Out, Mera400::RW, Mera400::RW_EXT, Reg(0), Reg(1),
                       Extra(2));
  case Mera400::STWabs:
    return lowerExtArg(Out, Mera400::RW_EXT, Reg(0), Extra(1));
  case Mera400::AFrm:
    return lowerNoRegMemArg(Out, Mera400::AF, Mera400::AF_EXT, Reg(0),
                            Extra(1));
  case Mera400::SFrm:
    return lowerNoRegMemArg(Out, Mera400::SF, Mera400::SF_EXT, Reg(0),
                            Extra(1));
  case Mera400::MFrm:
    return lowerNoRegMemArg(Out, Mera400::MF, Mera400::MF_EXT, Reg(0),
                            Extra(1));
  case Mera400::DFrm:
    return lowerNoRegMemArg(Out, Mera400::DF, Mera400::DF_EXT, Reg(0),
                            Extra(1));
  case Mera400::LDBrm:
  case Mera400::LDBabs: {
    // "lb" keeps the destination's high octet, so clear it first.
    Out.setOpcode(Mera400::LWT);
    addReg(Out, Reg(0));
    Out.addOperand(MCOperand::createImm(0));
    MCInst &LB = Insts.emplace_back();
    if (MI->getOpcode() == Mera400::LDBrm)
      return lowerMemArg(LB, Mera400::LB, Mera400::LB_EXT, Reg(0), Reg(1),
                         Extra(2));
    return lowerExtArg(LB, Mera400::LB_EXT, Reg(0), Extra(1));
  }
  case Mera400::STBrm:
    return lowerMemArg(Out, Mera400::RB, Mera400::RB_EXT, Reg(0), Reg(1),
                       Extra(2));
  case Mera400::STBabs:
    return lowerExtArg(Out, Mera400::RB_EXT, Reg(0), Extra(1));
  case Mera400::SHL1:
    // Slz shifts left one bit; the machine shifts one bit per instruction.
    // Also converts a word address to a byte address.
    Out.setOpcode(Mera400::SLZ);
    addReg(Out, Reg(0));
    return;
  case Mera400::SHR1:
    Out.setOpcode(Mera400::SRZ);
    addReg(Out, Reg(0));
    return;
  case Mera400::CALL:
    // "rj r4, callee" stores the return address in R4 and jumps.
    return lowerExtArg(Out, Mera400::RJ_EXT, Mera400::R4, Extra(0));
  case Mera400::CALLr:
    return lowerRegArg(Out, Mera400::RJ, Mera400::R4, Reg(0));
  case Mera400::RET:
    // An unconditional jump through R4.
    Out.setOpcode(Mera400::UJ);
    Out.addOperand(MCOperand::createImm(registerControl(Mera400::R4)));
    return;
  case Mera400::JMP:
  case Mera400::JMPL:
    Insts.pop_back();
    return lowerJump(Insts, JK_UJ,
                     MI->getOpcode() == Mera400::JMP && branchIsShort(JK_UJ),
                     MI->getOperand(0), AP.OutContext);
  case Mera400::BR_CC:
  case Mera400::BR_CCI: {
    // The compare sets one of the L, E and G flags: "cw" compares as signed
    // numbers and "cl" as unsigned, and "cwt" takes a small signed constant,
    // so unsigned compares against a constant need the two-word "cl".
    auto CC = static_cast<Mera400::CondCode>(Imm(2));
    if (MI->getOpcode() == Mera400::BR_CC) {
      lowerRegArg(Out, Mera400::isUnsignedCC(CC) ? Mera400::CL : Mera400::CW,
                  Reg(0), Reg(1));
    } else if (Mera400::isUnsignedCC(CC)) {
      lowerExtArg(Out, Mera400::CL_EXT, Reg(0), Imm(1));
    } else {
      lowerShortOrExtArg(Out, Mera400::CWT, Mera400::CW_EXT, Reg(0), Imm(1));
    }
    JumpKind Kinds[2];
    unsigned NumJumps = jumpsFor(CC, Kinds);
    for (unsigned I = 0; I != NumJumps; ++I)
      lowerJump(Insts, Kinds[I], branchIsShort(Kinds[I]), MI->getOperand(3),
                AP.OutContext);
    return;
  }
  case Mera400::ADDO:
    // Carry out as a 0/1 value: "aw" sets C; "lwt" does not change flags, and
    // "ac c, c" adds 0 + 0 + C.
    Insts.pop_back();
    emitReg(Insts, Mera400::AW, Reg(0), Reg(3));
    emitShort(Insts, Mera400::LWT, Reg(1), 0);
    emitReg(Insts, Mera400::AC, Reg(1), Reg(1));
    return;
  case Mera400::ADDCO:
    // Carry in: "awt co, -1" sets C exactly when co (the carry) is 1.
    Insts.pop_back();
    emitShort(Insts, Mera400::AWT, Reg(1), -1);
    emitReg(Insts, Mera400::AC, Reg(0), Reg(3));
    emitShort(Insts, Mera400::LWT, Reg(1), 0);
    emitReg(Insts, Mera400::AC, Reg(1), Reg(1));
    return;
  case Mera400::SUBO:
    // "sw" sets C when there is no borrow, so borrow = 1 - C; computed as
    // (0xFFFE + C) then bitwise-negated.
    Insts.pop_back();
    emitReg(Insts, Mera400::SW, Reg(0), Reg(3));
    emitShort(Insts, Mera400::LWT, Reg(1), -1);
    emitReg(Insts, Mera400::AC, Reg(1), Reg(1));
    emitUnary(Insts, Mera400::NGL, Reg(1));
    return;
  case Mera400::SUBBO:
    Insts.pop_back();
    emitUnary(Insts, Mera400::NGL, Reg(0));
    emitShort(Insts, Mera400::AWT, Reg(1), -1);
    emitReg(Insts, Mera400::AC, Reg(0), Reg(3));
    emitUnary(Insts, Mera400::NGL, Reg(0));
    emitShort(Insts, Mera400::LWT, Reg(1), 0);
    emitReg(Insts, Mera400::AC, Reg(1), Reg(1));
    return;
  case Mera400::ADD32:
    Insts.pop_back();
    emitReg(Insts, Mera400::AW, Reg(1), Reg(5));
    emitReg(Insts, Mera400::AC, Reg(0), Reg(4));
    return;
  case Mera400::SUB32:
    Insts.pop_back();
    emitReg(Insts, Mera400::SW, Reg(1), Reg(6));
    emitReg(Insts, Mera400::LW, Reg(2), Reg(5));
    emitUnary(Insts, Mera400::NGL, Reg(2));
    emitReg(Insts, Mera400::AC, Reg(0), Reg(2));
    return;
  case Mera400::SRA1:
    Insts.pop_back();
    emitUnary(Insts, Mera400::SXU, Reg(0));
    emitUnary(Insts, Mera400::SRX, Reg(0));
    return;
  case Mera400::SIGNMASK:
    Insts.pop_back();
    emitUnary(Insts, Mera400::SXU, Reg(1));
    emitShort(Insts, Mera400::LWT, Reg(0), 0);
    emitUnary(Insts, Mera400::SLX, Reg(0));
    emitUnary(Insts, Mera400::NGA, Reg(0));
    return;
  case Mera400::SHL32_1:
    Insts.pop_back();
    emitUnary(Insts, Mera400::SLZ, Reg(1));
    emitUnary(Insts, Mera400::SLY, Reg(0));
    return;
  case Mera400::SRL32_1:
    Insts.pop_back();
    emitUnary(Insts, Mera400::SRZ, Reg(0));
    emitUnary(Insts, Mera400::SRY, Reg(1));
    return;
  case Mera400::SRA32_1:
    Insts.pop_back();
    emitUnary(Insts, Mera400::SXU, Reg(0));
    emitUnary(Insts, Mera400::SRX, Reg(0));
    emitUnary(Insts, Mera400::SRY, Reg(1));
    return;
  case Mera400::SHCri:
    Out.setOpcode(Mera400::SHC);
    addReg(Out, Reg(0));
    Out.addOperand(MCOperand::createImm(Imm(2)));
    return;
  default:
    report_fatal_error("Mera400: cannot lower this instruction to assembly");
  }
}
