//===-- Mera400.td - MERA-400 target description -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Describes the MERA-400 to TableGen: subtarget features, the eight 16-bit
// registers, the operand kinds and the real (assembler level) instructions
// with their bit encodings. Instruction selection uses the pseudo instructions
// in Mera400InstrInfo.td instead. Notes on hardware behavior:
//   - R0 doubles as the flags register (Z, M, V, C, L, E, G, Y, X).
//   - Arithmetic sets Z, M, V and C; logic ("nr" AND, "or", "xr" XOR, "er"
//     AND-NOT) sets Z only; "cw" (signed) and "cl" (unsigned) compare and set
//     only one of L, E, G; loads and stores leave the flags alone.
//   - Shifts move one bit per instruction; "shc" rotates by a constant count.
//   - Short ("s") jumps and "t" instructions embed a 6-bit sign-magnitude
//     value in the instruction word, avoiding the extra word of the general
//     form.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

// MX-16 is the later CPU revision that adds CRON, SINT and SIND.
def FeatureMX16 : SubtargetFeature<"mx16", "HasMX16", "true",
                                   "Enable MX-16 instructions">;

def FeatureLargeMemory : SubtargetFeature<"large-memory", "HasLargeMemory",
                                          "true",
                                          "Use 32-bit linear byte pointers "
                                          "(the large memory model)">;

// AWP is the optional arithmetic (floating point) processor. Without it, the
// four basic floating point operations are library calls.
def FeatureAWP : SubtargetFeature<"awp", "HasAWP", "true",
                                  "Use the AWP floating point instructions">;

def : Processor<"generic", NoItineraries, []>;
def : Processor<"mera400", NoItineraries, []>;
def : Processor<"mx16", NoItineraries, [FeatureMX16]>;

// Encoding is the register number itself: the register fields of an
// instruction word (A, B and C below) are 3 bits wide, so R0..R7 are simply
// 0..7 and the same value serves the assembler, the code emitter and the
// disassembler.
//
// Instruction words are 16 bits, and a "normal argument" instruction may be
// followed by a second 16-bit word. The classes below declare the layout with
// "Inst" bit assignments because TableGen derives both the encoder and the
// disassembler decoder tables from them: every bit of the word must be
// assigned to an opcode constant or an operand field. The general layout is
//     15..10  OP  opcode (6 bits)
//        9    D   normal argument: the argument is indirect (the value is
//                 used as an address); other forms reuse this bit (below)
//      8..6   A   register operand, or a sub-opcode when there is no register
//      5..3   B   index register (0 = none), added to the argument
//      2..0   C   register holding the argument; C = 0 means the argument is
//                 the 16-bit word that follows the instruction
// Instruction words are stored big-endian, first word first. The hardware
// fetches the argument as: C (register or following word), plus B, then, if D
// is set, one memory load through the sum.
class MeraReg<bits<3> Encoding, string Name> : Register<Name> {
  let Namespace = "Mera400";
  let HWEncoding{2-0} = Encoding;
}

def R0 : MeraReg<0, "r0">;
def R1 : MeraReg<1, "r1">;
def R2 : MeraReg<2, "r2">;
def R3 : MeraReg<3, "r3">;
def R4 : MeraReg<4, "r4">;
def R5 : MeraReg<5, "r5">;
def R6 : MeraReg<6, "r6">;
def R7 : MeraReg<7, "r7">;

// Every register an instruction can name; this is what the assembler uses.
def GR16 : RegisterClass<"Mera400", [i16], 16,
                         (add R0, R1, R2, R3, R4, R5, R6, R7)>;

// Registers that code generation may hold values in. R0 (flags) is left out,
// and R4 (return address) and R7 (stack pointer) are reserved in
// Mera400RegisterInfo::getReservedRegs. The allocation order tries the
// caller-saved argument registers first.
def GPR16 : RegisterClass<"Mera400", [i16], 16,
                          (add R1, R2, R3, R5, R6, R4, R7)>;

// Required by TableGen; code generation and the pointer ABI are not defined.
defm : RemapAllTargetPseudoPointerOperands<GR16>;

def ShortImmAsmOperand : AsmOperandClass {
  let Name = "ShortImm";
  let RenderMethod = "addImmOperands";
}
def shortimm : Operand<i16> {
  let ParserMatchClass = ShortImmAsmOperand;
}

// Short operand of a branch: either a literal 7-bit value or a label, which
// the assembler turns into a PC-relative offset through a fixup.
def ShortBrAsmOperand : AsmOperandClass {
  let Name = "ShortBr";
  let RenderMethod = "addShortBrOperands";
  let PredicateMethod = "isShortBr";
}
def shortbr : Operand<i16> {
  let ParserMatchClass = ShortBrAsmOperand;
  let PrintMethod = "printShortBrOperand";
  let EncoderMethod = "getShortBrOpValue";
}

// Short value operand (awt, cwt, lwt, trb): a literal 7-bit value or a label
// whose word address the linker fills in through a relocation.
def ShortValAsmOperand : AsmOperandClass {
  let Name = "ShortVal";
  let RenderMethod = "addShortBrOperands";
  let PredicateMethod = "isShortVal";
}
def shortval : Operand<i16> {
  let ParserMatchClass = ShortValAsmOperand;
  let PrintMethod = "printShortBrOperand";
  let EncoderMethod = "getShortValOpValue";
}

// Short-argument group (opcodes 0b110000..0b110111): opcode, register A and a
// 6-bit immediate fit in one word, so no extra word is needed. The immediate
// is sign-magnitude, not two's complement: the magnitude is in bits 5..0 and
// the sign is in bit 9, the position that D occupies in the normal form, so
// -1 is 0x41 in the field and "lwt r2, -1" is 0xd681. The assembler operand
// has 7 bits (sign above magnitude), with bit 6 of "imm" mapped to bit 9 of
// the word; hence the split bit assignment below. The range is -63..63; "-0"
// has a second encoding that the disassembler rejects.
// awt, cwt, lwt: add/compare/load the immediate; trb: add the immediate and
// skip the next instruction if the result is zero; irb/drb: increment/
// decrement the register and, unless it became zero, jump by the immediate
// words from the next instruction; lws/rws: load/store the word at that
// word offset from the next instruction.
class ShortInst<bits<6> Opcode, string Mnemonic, Operand ImmOp = shortval>
    : Instruction {
  bits<3> reg;
  bits<7> imm;
  bits<16> Inst;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins GR16:$reg, ImmOp:$imm);
  let AsmString = !strconcat(Mnemonic, "\t$reg, $imm");
  let Inst{15-10} = Opcode;
  let Inst{9} = imm{6};
  let Inst{8-6} = reg;
  let Inst{5-0} = imm{5-0};
}

def AWT : ShortInst<0b110000, "awt">;
let isBranch = 1, isTerminator = 1, hasSideEffects = 1 in {
def TRB : ShortInst<0b110001, "trb">;
def IRB : ShortInst<0b110010, "irb", shortbr>;
def DRB : ShortInst<0b110011, "drb", shortbr>;
}
def CWT : ShortInst<0b110100, "cwt">;
def LWT : ShortInst<0b110101, "lwt">;
let mayLoad = 1 in
def LWS : ShortInst<0b110110, "lws", shortbr>;
let mayStore = 1 in
def RWS : ShortInst<0b110111, "rws", shortbr>;

// Register-only group (opcode 0b111010): the operation is in bits 5..0 and bit
// 9 selects one of two variants, so the same operation number appears twice
// (0: ric/rky, 1: zlb/zrb, 2: sxu/sxl, 3: nga/ngc, 4: slz/svz, 5: sly/svy,
// 6: slx/svx, 7: sry/srx, 8: ngl/srz, 9: rpc/lpc). Shifts here move ONE bit
// per instruction, so a multi-bit shift is a chain of them. The pattern
// 0b010xxx in bits 5..3 is "shc" instead, where bit 9 is part of the count.
class RegInst<bit Variant, bits<6> Operation, string Mnemonic> : Instruction {
  bits<3> reg;
  bits<16> Inst;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins GR16:$reg);
  let AsmString = !strconcat(Mnemonic, "\t$reg");
  let Inst{15-10} = 0b111010;
  let Inst{9} = Variant;
  let Inst{8-6} = reg;
  let Inst{5-0} = Operation;
  let hasSideEffects = 1;
}

def RIC : RegInst<0, 0b000000, "ric">;
def ZLB : RegInst<0, 0b000001, "zlb">;
def SXU : RegInst<0, 0b000010, "sxu">;
def NGA : RegInst<0, 0b000011, "nga">;
def SLZ : RegInst<0, 0b000100, "slz">;
def SLY : RegInst<0, 0b000101, "sly">;
def SLX : RegInst<0, 0b000110, "slx">;
def SRY : RegInst<0, 0b000111, "sry">;
def NGL : RegInst<0, 0b001000, "ngl">;
def RPC : RegInst<0, 0b001001, "rpc">;
def RKY : RegInst<1, 0b000000, "rky">;
def ZRB : RegInst<1, 0b000001, "zrb">;
def SXL : RegInst<1, 0b000010, "sxl">;
def NGC : RegInst<1, 0b000011, "ngc">;
def SVZ : RegInst<1, 0b000100, "svz">;
def SVY : RegInst<1, 0b000101, "svy">;
def SVX : RegInst<1, 0b000110, "svx">;
def SRX : RegInst<1, 0b000111, "srx">;
def SRZ : RegInst<1, 0b001000, "srz">;
def LPC : RegInst<1, 0b001001, "lpc">;

// Short jumps (opcode 0b111000): the condition is in bits 8..6, the field that
// holds the register in other groups, because a jump has none. The offset is a
// sign-magnitude word count from the next instruction (same encoding as the
// short immediates, so -63..63 words). The long forms (0b111100) also have
// "jz" and "jm" (Z and M flags) and "jn" (E flag clear); the short forms exist
// only for L, E, G, V, X, Y and C.
class ShortBranch<bits<3> Condition, string Mnemonic> : Instruction {
  bits<7> imm;
  bits<16> Inst;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins shortbr:$imm);
  let AsmString = !strconcat(Mnemonic, "\t$imm");
  let Inst{15-10} = 0b111000;
  let Inst{9} = imm{6};
  let Inst{8-6} = Condition;
  let Inst{5-0} = imm{5-0};
  let isBranch = 1;
  let isTerminator = 1;
}

let isBarrier = 1 in
def UJS : ShortBranch<0b000, "ujs">;
def JLS : ShortBranch<0b001, "jls">;
def JES : ShortBranch<0b010, "jes">;
def JGS : ShortBranch<0b011, "jgs">;
def JVS : ShortBranch<0b100, "jvs">;
def JXS : ShortBranch<0b101, "jxs">;
def JYS : ShortBranch<0b110, "jys">;
def JCS : ShortBranch<0b111, "jcs">;

// Normal argument: a 7-bit control field n = {D, B, C} (indirect bit, index
// register, register C) with an optional extra 16-bit word. C = 0 selects the
// extra word, which holds a value added to the index register B. In the
// one-word form n sits in the word as bit 9 = n{6} and bits 5..0 = n{5-0}; the
// register or sub-opcode field A (bits 8..6) sits between them, which is why
// n is not one contiguous bit range. In the two-word "_EXT" form the first word
// is Inst{31-16} (big-endian, first word in the high half): the control bits
// move up by 16, so D = n{22} lands in bit 25, A in 24..22 and B, C in
// 21..16, and the extra word is Inst{15-0} = n{15-0}. The code emitter puts
// the control field at bit 16 and the extra word at bit 0 of the operand;
// C is 0 in that form because C = 0 is what announces the extra word.
def NormRegAsmOperand : AsmOperandClass {
  let Name = "NormReg";
  let RenderMethod = "addNormRegOperands";
  let PredicateMethod = "isNormReg";
}
def normreg : Operand<i16> {
  let ParserMatchClass = NormRegAsmOperand;
  let PrintMethod = "printNormOperand";
  let EncoderMethod = "getNormRegOpValue";
}

def NormExtAsmOperand : AsmOperandClass {
  let Name = "NormExt";
  let RenderMethod = "addNormExtOperands";
  let PredicateMethod = "isNormExt";
}
def normext : Operand<i16> {
  let ParserMatchClass = NormExtAsmOperand;
  let PrintMethod = "printNormOperand";
  let EncoderMethod = "getNormExtOpValue";
  let MIOperandInfo = (ops i16imm:$ctrl, i16imm:$ext);
}

// Instruction with a normal argument. $a is the register field; instructions
// without a register operand have it fixed to a constant selector.
class NormRegForm<bits<6> Opcode, string Mnemonic> : Instruction {
  bits<3> a;
  bits<7> n;
  bits<16> Inst;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins GR16:$a, normreg:$n);
  let AsmString = !strconcat(Mnemonic, "\t$a, $n");
  let Inst{15-10} = Opcode;
  let Inst{9} = n{6};
  let Inst{8-6} = a;
  let Inst{5-0} = n{5-0};
  let hasSideEffects = 1;
}
class NormExtForm<bits<6> Opcode, string Mnemonic> : Instruction {
  bits<3> a;
  bits<23> n;
  bits<32> Inst;
  let Namespace = "Mera400";
  let Size = 4;
  let OutOperandList = (outs);
  let InOperandList = (ins GR16:$a, normext:$n);
  let AsmString = !strconcat(Mnemonic, "\t$a, $n");
  let Inst{31-26} = Opcode;
  let Inst{25} = n{22};
  let Inst{24-22} = a;
  let Inst{21-16} = n{21-16};
  let Inst{15-0} = n{15-0};
  let hasSideEffects = 1;
}
// Instruction without a register operand: the A field (bits 8..6) is free, so
// it is reused as a sub-opcode ("Selector") that picks one of eight
// instructions sharing the 6-bit opcode.
class NormNoRegForm<bits<6> Opcode, bits<3> Selector, string Mnemonic>
    : Instruction {
  bits<7> n;
  bits<16> Inst;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins normreg:$n);
  let AsmString = !strconcat(Mnemonic, "\t$n");
  let Inst{15-10} = Opcode;
  let Inst{9} = n{6};
  let Inst{8-6} = Selector;
  let Inst{5-0} = n{5-0};
  let hasSideEffects = 1;
}
class NormNoRegExtForm<bits<6> Opcode, bits<3> Selector, string Mnemonic>
    : Instruction {
  bits<23> n;
  bits<32> Inst;
  let Namespace = "Mera400";
  let Size = 4;
  let OutOperandList = (outs);
  let InOperandList = (ins normext:$n);
  let AsmString = !strconcat(Mnemonic, "\t$n");
  let Inst{31-26} = Opcode;
  let Inst{25} = n{22};
  let Inst{24-22} = Selector;
  let Inst{21-16} = n{21-16};
  let Inst{15-0} = n{15-0};
  let hasSideEffects = 1;
}

multiclass NormReg<bits<6> Opcode, string Mnemonic> {
  def NAME : NormRegForm<Opcode, Mnemonic>;
  def NAME#"_EXT" : NormExtForm<Opcode, Mnemonic>;
}
multiclass NormNoReg<bits<6> Opcode, bits<3> Selector, string Mnemonic> {
  def NAME : NormNoRegForm<Opcode, Selector, Mnemonic>;
  def NAME#"_EXT" : NormNoRegExtForm<Opcode, Selector, Mnemonic>;
}

// Opcodes are checked against the EMAS instruction table:
// https://github.com/jakubfi/emas/blob/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4/src/keywords.c
//
// Normal-argument groups by 6-bit opcode:
//   0b010000..0b011110  transfer and branch-on-register group: lw (load the
//                       argument value), tw (load from memory), ls, ri, rw
//                       (store), pw, rj (jump, saving the return address in
//                       the register), is, bb, bm, bs, bc, bn (bit tests that
//                       skip the next instruction on a condition), ou, in
//                       (input/output)
//   0b100000..0b101111  arithmetic, logic, compare and byte group: aw, ac,
//                       sw, cw, or, om, nr, nm, er, em, xr, xm, cl, lb, rb, cb.
//                       Note er is AND-NOT.
//   0b011111            no register operand; A is a sub-opcode: ad, sd
//                       (double-word add, subtract), mw, dw (multiply,
//                       divide) and af, sf, mf, df (floating point), the
//                       optional AWP unit's instructions.
//   0b111100            jumps, selected by the A field: uj and the long
//                       conditional jumps, plus lj
//   0b111101 / 0b111110 load / store of several consecutive registers from /
//                       to memory, selected by the A field: ld, lf, la, ll /
//                       rd, rf, ra, rl, and the td, tf, ta, tl / pd, pf, pa,
//                       pl variants, which use a fixed memory block selector
//                       instead of the current mode. They move 2 (d), 3 (f),
//                       7 (a: r1..r7) or 3 (l: r5..r7) words, the lowest
//                       register at the lowest address.
//   0b111111            system group: mb, im, ki, fi, sp (not allowed in user
//                       mode), md, rz, ib
defm LW : NormReg<0b010000, "lw">;
defm TW : NormReg<0b010001, "tw">;
defm LS : NormReg<0b010010, "ls">;
defm RI : NormReg<0b010011, "ri">;
defm RW : NormReg<0b010100, "rw">;
defm PW : NormReg<0b010101, "pw">;
defm RJ : NormReg<0b010110, "rj">;
defm IS : NormReg<0b010111, "is">;
defm BB : NormReg<0b011000, "bb">;
defm BM : NormReg<0b011001, "bm">;
defm BS : NormReg<0b011010, "bs">;
defm BC : NormReg<0b011011, "bc">;
defm BN : NormReg<0b011100, "bn">;
defm OU : NormReg<0b011101, "ou">;
defm IN : NormReg<0b011110, "in">;
defm AW : NormReg<0b100000, "aw">;
defm AC : NormReg<0b100001, "ac">;
defm SW : NormReg<0b100010, "sw">;
defm CW : NormReg<0b100011, "cw">;
defm OR : NormReg<0b100100, "or">;
defm OM : NormReg<0b100101, "om">;
defm NR : NormReg<0b100110, "nr">;
defm NM : NormReg<0b100111, "nm">;
defm ER : NormReg<0b101000, "er">;
defm EM : NormReg<0b101001, "em">;
defm XR : NormReg<0b101010, "xr">;
defm XM : NormReg<0b101011, "xm">;
defm CL : NormReg<0b101100, "cl">;
defm LB : NormReg<0b101101, "lb">;
defm RB : NormReg<0b101110, "rb">;
defm CB : NormReg<0b101111, "cb">;

defm AD : NormNoReg<0b011111, 0b000, "ad">;
defm SD : NormNoReg<0b011111, 0b001, "sd">;
defm MW : NormNoReg<0b011111, 0b010, "mw">;
defm DW : NormNoReg<0b011111, 0b011, "dw">;
defm AF : NormNoReg<0b011111, 0b100, "af">;
defm SF : NormNoReg<0b011111, 0b101, "sf">;
defm MF : NormNoReg<0b011111, 0b110, "mf">;
defm DF : NormNoReg<0b011111, 0b111, "df">;

let isBranch = 1, isTerminator = 1 in {
let isBarrier = 1 in
defm UJ : NormNoReg<0b111100, 0b000, "uj">;
defm JL : NormNoReg<0b111100, 0b001, "jl">;
defm JE : NormNoReg<0b111100, 0b010, "je">;
defm JG : NormNoReg<0b111100, 0b011, "jg">;
defm JZ : NormNoReg<0b111100, 0b100, "jz">;
defm JM : NormNoReg<0b111100, 0b101, "jm">;
defm JN : NormNoReg<0b111100, 0b110, "jn">;
}
defm LJ : NormNoReg<0b111100, 0b111, "lj">;

defm LD : NormNoReg<0b111101, 0b000, "ld">;
defm LF : NormNoReg<0b111101, 0b001, "lf">;
defm LA : NormNoReg<0b111101, 0b010, "la">;
defm LL : NormNoReg<0b111101, 0b011, "ll">;
defm TD : NormNoReg<0b111101, 0b100, "td">;
defm TF : NormNoReg<0b111101, 0b101, "tf">;
defm TA : NormNoReg<0b111101, 0b110, "ta">;
defm TL : NormNoReg<0b111101, 0b111, "tl">;

defm RD : NormNoReg<0b111110, 0b000, "rd">;
defm RF : NormNoReg<0b111110, 0b001, "rf">;
defm RA : NormNoReg<0b111110, 0b010, "ra">;
defm RL : NormNoReg<0b111110, 0b011, "rl">;
defm PD : NormNoReg<0b111110, 0b100, "pd">;
defm PF : NormNoReg<0b111110, 0b101, "pf">;
defm PA : NormNoReg<0b111110, 0b110, "pa">;
defm PL : NormNoReg<0b111110, 0b111, "pl">;

defm MB : NormNoReg<0b111111, 0b000, "mb">;
defm IM : NormNoReg<0b111111, 0b001, "im">;
defm KI : NormNoReg<0b111111, 0b010, "ki">;
defm FI : NormNoReg<0b111111, 0b011, "fi">;
defm SP : NormNoReg<0b111111, 0b100, "sp">;
defm MD : NormNoReg<0b111111, 0b101, "md">;
defm RZ : NormNoReg<0b111111, 0b110, "rz">;
defm IB : NormNoReg<0b111111, 0b111, "ib">;

class RangeAsmOperand<string ClassName, int Lo, int Hi> : AsmOperandClass {
  let Name = ClassName;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImmRange<" # Lo # "," # Hi # ">";
}
class RangeOperand<string Name, int Lo, int Hi, string Encoder = "">
    : Operand<i16> {
  let ParserMatchClass = RangeAsmOperand<Name, Lo, Hi>;
  let EncoderMethod = Encoder;
}

// SHC rotates the register RIGHT by a count; the assembler accepts -15..15 and
// a negative count rotates left. The hardware field is a 4-bit unsigned count
// (0..15), so a negative count -n is stored as 16 - n (rotating right by 16 - n
// equals rotating left by n). Unlike the sign-magnitude short immediates, the
// 4 bits are split: the low three go to bits 2..0 and the top bit to bit 9,
// because bits 5..3 hold the 0b010 that marks this operation in the
// register-only group and bits 8..6 hold the register.
def shiftcount : RangeOperand<"ShiftCount", -15, 15, "getShiftCountOpValue">;
def SHC : Instruction {
  bits<3> reg;
  bits<4> cnt;
  bits<16> Inst;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins GR16:$reg, shiftcount:$cnt);
  let AsmString = "shc\t$reg, $cnt";
  let Inst{15-10} = 0b111010;
  let Inst{9} = cnt{3};
  let Inst{8-6} = reg;
  let Inst{5-3} = 0b010;
  let Inst{2-0} = cnt{2-0};
  let hasSideEffects = 1;
}

// hlt (opcode 0b111011) stops the CPU; its 7-bit code is stored like the
// short-jump field (bit 9 and bits 5..0) with bits 8..6 zero, but is an
// unsigned value 0..127 here, not a sign and magnitude.
// Opcode 0b111001 is the byte-immediate group: bits 9..8 select blc, exl, brc
// or nrf (the full word is 0xe4..0xe7 in the top byte) and the low byte is the
// immediate.
// HLT, BLC, BRC, EXL and NRF carry an immediate inside the opcode word.
def immhlt : RangeOperand<"HaltCode", 0, 127>;
def HLT : Instruction {
  bits<7> imm;
  bits<16> Inst;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins immhlt:$imm);
  let AsmString = "hlt\t$imm";
  let Inst{15-10} = 0b111011;
  let Inst{9} = imm{6};
  let Inst{8-6} = 0;
  let Inst{5-0} = imm{5-0};
  let hasSideEffects = 1;
}
def : InstAlias<"hlt", (HLT 0)>;

class ByteInst<bits<8> Opcode, string Mnemonic, Operand Op> : Instruction {
  bits<8> imm;
  bits<16> Inst;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins Op:$imm);
  let AsmString = !strconcat(Mnemonic, "\t$imm");
  let Inst{15-8} = Opcode;
  let Inst{7-0} = imm;
  let hasSideEffects = 1;
}
// BLC takes a value whose only set bits are in the left byte.
def LeftByteAsmOperand : AsmOperandClass {
  let Name = "LeftByte";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isLeftByteImm";
}
def immblc : Operand<i16> {
  let ParserMatchClass = LeftByteAsmOperand;
  let EncoderMethod = "getLeftByteOpValue";
}
def immbyte : RangeOperand<"Byte", 0, 255>;
def BLC : ByteInst<0xe4, "blc", immblc>;
def EXL : ByteInst<0xe5, "exl", immbyte>;
def BRC : ByteInst<0xe6, "brc", immbyte>;
def NRF : ByteInst<0xe7, "nrf", immbyte>;
def : InstAlias<"nrf", (NRF 255)>;

// Instructions with no operands are whole 16-bit constants. All of them live
// in the 0b111011 group (the one hlt is in): A (bits 8..6) selects the family
// and the low bits the member, e.g. cit, sil, siu, sit differ only in bits 1..0
// of 0xec80. They are illegal in user mode.
// CRON, SINT and SIND exist only on the MX-16 CPU revision.
class FixedInst<bits<16> Encoding, string Mnemonic> : Instruction {
  bits<16> Inst = Encoding;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = Mnemonic;
  let hasSideEffects = 1;
}
def MCL : FixedInst<0xec40, "mcl">;
def CIT : FixedInst<0xec80, "cit">;
def SIL : FixedInst<0xec81, "sil">;
def SIU : FixedInst<0xec82, "siu">;
def SIT : FixedInst<0xec83, "sit">;
def GIU : FixedInst<0xecc0, "giu">;
def LIP : FixedInst<0xed00, "lip">;
def GIL : FixedInst<0xeec0, "gil">;
// Valid only after ".cpu mx16"; the assembler parser enforces that.
def CRON : FixedInst<0xed40, "cron">;
def SINT : FixedInst<0xec84, "sint">;
def SIND : FixedInst<0xee84, "sind">;

// The machine has no nop; "ujs 0" (jump by zero words, 0xe000) does nothing
// and is what the assembler backend uses for padding.
def NOP : Instruction {
  bits<16> Inst = 0xe000;
  let Namespace = "Mera400";
  let Size = 2;
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = "nop";
}

include "Mera400CallingConv.td"
include "Mera400InstrInfo.td"

def Mera400InstrInfo : InstrInfo;
def Mera400AsmWriter : AsmWriter;
def Mera400AsmParser : AsmParser;
def Mera400 : Target {
  let InstructionSet = Mera400InstrInfo;
  let AssemblyParsers = [Mera400AsmParser];
}
