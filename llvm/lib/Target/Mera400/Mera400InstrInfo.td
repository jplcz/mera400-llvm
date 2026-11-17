//===-- Mera400InstrInfo.td - MERA-400 code generation instructions -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The real instructions in Mera400.td describe the assembler syntax: they have
// no output operands and carry the packed "normal argument" control field.
// Instruction selection needs explicit inputs and outputs, so the instructions
// below are pseudo instructions. Mera400MCInstLower.cpp turns each of them into
// one real instruction when assembly is printed.
//
//===----------------------------------------------------------------------===//

def Mera400retglue : SDNode<"Mera400ISD::RET_GLUE", SDTNone,
                            [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def SDT_Mera400Call : SDTypeProfile<0, 1, [SDTCisVT<0, i16>]>;
def Mera400call : SDNode<"Mera400ISD::CALL", SDT_Mera400Call,
                         [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                          SDNPVariadic]>;
def Mera400wrapper : SDNode<"Mera400ISD::WRAPPER", SDTIntUnaryOp>;
def Mera400shl1 : SDNode<"Mera400ISD::SHL1", SDTIntUnaryOp>;
def Mera400shr1 : SDNode<"Mera400ISD::SHR1", SDTIntUnaryOp>;
def SDT_Mera400CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_Mera400CallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
// Compare-and-branch and compare-and-select: operands are lhs, rhs, a condition
// code (Mera400CC in Mera400.h) and the destination or the two values.
def SDT_Mera400BrCC : SDTypeProfile<0, 4, [SDTCisVT<0, i16>, SDTCisSameAs<1, 0>,
                                           SDTCisVT<2, i16>,
                                           SDTCisVT<3, OtherVT>]>;
def Mera400brcc : SDNode<"Mera400ISD::BR_CC", SDT_Mera400BrCC, [SDNPHasChain]>;
def SDT_Mera400SelectCC : SDTypeProfile<1, 5, [SDTCisVT<0, i16>,
                                               SDTCisSameAs<1, 2>,
                                               SDTCisVT<3, i16>,
                                               SDTCisSameAs<4, 0>,
                                               SDTCisSameAs<5, 0>]>;
def Mera400selectcc : SDNode<"Mera400ISD::SELECT_CC", SDT_Mera400SelectCC>;
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_Mera400CallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_Mera400CallSeqEnd,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

class Mera400Pseudo<dag outs, dag ins, list<dag> pattern, int size>
    : Instruction {
  let Namespace = "Mera400";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = "";
  let Pattern = pattern;
  let Size = size;
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

// Two-address arithmetic: the destination is also the first operand, as in
// the hardware ("aw r1, r2" means r1 = r1 + r2). The immediate form takes a
// 16-bit constant that follows the instruction word.
multiclass Mera400BinOp<SDPatternOperator Op, bit Commutable> {
  let Constraints = "$d = $s", hasSideEffects = 0,
      isCommutable = Commutable in
  def rr : Mera400Pseudo<(outs GPR16:$d), (ins GPR16:$s, GPR16:$t),
                         [(set GPR16:$d, (Op GPR16:$s, GPR16:$t))], 2>;
}
multiclass Mera400BinOpImm<SDPatternOperator Op, bit Commutable>
    : Mera400BinOp<Op, Commutable> {
  let Constraints = "$d = $s", hasSideEffects = 0 in
  def ri : Mera400Pseudo<(outs GPR16:$d), (ins GPR16:$s, i16imm:$imm),
                         [(set GPR16:$d, (Op GPR16:$s, imm:$imm))], 4>;
}

defm ADD : Mera400BinOpImm<add, 1>;
defm SUB : Mera400BinOp<sub, 0>;
defm AND : Mera400BinOpImm<and, 1>;
defm OR : Mera400BinOpImm<or, 1>;
defm XOR : Mera400BinOpImm<xor, 1>;

let hasSideEffects = 0 in {
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def MOVri : Mera400Pseudo<(outs GPR16:$d), (ins i16imm:$imm),
                          [(set GPR16:$d, imm:$imm)], 4>;
let isMoveReg = 1 in
def MOVrr : Mera400Pseudo<(outs GPR16:$d), (ins GPR16:$s), [], 2>;
}

// Return is a jump through R4, which "rj" filled with the return address.
let isReturn = 1, isTerminator = 1, isBarrier = 1, Uses = [R4] in
def RET : Mera400Pseudo<(outs), (ins), [(Mera400retglue)], 2>;

// Memory operands. The effective address is base register + displacement in
// the units of the pointer: words for address space 0, bytes for address space
// 1. A base that is a frame index is replaced by R7 after register allocation.
def memrm : Operand<i16> {
  let MIOperandInfo = (ops GPR16:$base, i16imm:$disp);
}
def memabs : Operand<i16>;
def addrrm : ComplexPattern<iPTR, 2, "SelectAddrRM", [], []>;
def addrabs : ComplexPattern<iPTR, 1, "SelectAddrAbs", [], []>;

def load_as0 : PatFrag<(ops node:$ptr), (load node:$ptr)> {
  let AddressSpaces = [0];
}
def store_as0 : PatFrag<(ops node:$v, node:$ptr), (store node:$v, node:$ptr)> {
  let AddressSpaces = [0];
}
// The address space is checked in code: a byte instruction takes a byte
// address, so a byte access through a word pointer (address space 0) must not
// be selected (it used the word address as a byte address); that is an error
// instead of a silent miscompile.
def zextloadi8_as1 : PatFrag<(ops node:$ptr), (zextloadi8 node:$ptr), [{
  return cast<MemSDNode>(N)->getAddressSpace() == 1;
}]> {
  let AddressSpaces = [1];
}
def extloadi8_as1 : PatFrag<(ops node:$ptr), (extloadi8 node:$ptr), [{
  return cast<MemSDNode>(N)->getAddressSpace() == 1;
}]> {
  let AddressSpaces = [1];
}
def truncstorei8_as1 : PatFrag<(ops node:$v, node:$ptr),
                               (truncstorei8 node:$v, node:$ptr), [{
  return cast<MemSDNode>(N)->getAddressSpace() == 1;
}]> {
  let AddressSpaces = [1];
}

// Words: "tw rA, disp+rB" loads, "rw rA, disp+rB" stores.
let mayLoad = 1, hasSideEffects = 0 in {
def LDWrm : Mera400Pseudo<(outs GPR16:$d), (ins memrm:$a),
                          [(set GPR16:$d, (load_as0 addrrm:$a))], 4>;
def LDWabs : Mera400Pseudo<(outs GPR16:$d), (ins memabs:$a),
                           [(set GPR16:$d, (load_as0 addrabs:$a))], 4>;
}
let mayStore = 1, hasSideEffects = 0 in {
def STWrm : Mera400Pseudo<(outs), (ins GPR16:$s, memrm:$a),
                          [(store_as0 GPR16:$s, addrrm:$a)], 4>;
def STWabs : Mera400Pseudo<(outs), (ins GPR16:$s, memabs:$a),
                           [(store_as0 GPR16:$s, addrabs:$a)], 4>;
}

// Bytes: "lb" keeps the high octet of the destination, so the destination is
// cleared first ("lwt rA, 0"); it must not be the address register.
let mayLoad = 1, hasSideEffects = 0, Constraints = "@earlyclobber $d" in {
def LDBrm : Mera400Pseudo<(outs GPR16:$d), (ins memrm:$a),
                          [(set GPR16:$d, (zextloadi8_as1 addrrm:$a))], 6>;
def LDBabs : Mera400Pseudo<(outs GPR16:$d), (ins memabs:$a),
                           [(set GPR16:$d, (zextloadi8_as1 addrabs:$a))], 6>;
}
let mayStore = 1, hasSideEffects = 0 in {
def STBrm : Mera400Pseudo<(outs), (ins GPR16:$s, memrm:$a),
                          [(truncstorei8_as1 GPR16:$s, addrrm:$a)], 4>;
def STBabs : Mera400Pseudo<(outs), (ins GPR16:$s, memabs:$a),
                           [(truncstorei8_as1 GPR16:$s, addrabs:$a)], 4>;
}
def : Pat<(i16 (extloadi8_as1 addrrm:$a)), (LDBrm addrrm:$a)>;
def : Pat<(i16 (extloadi8_as1 addrabs:$a)), (LDBabs addrabs:$a)>;

// Address of a frame object: R7 + offset. It changes the flags (R0) because
// it is built with "aw"; code generation 3 must treat it as defining R0.
let hasSideEffects = 0, Defs = [R0] in
def LEA : Mera400Pseudo<(outs GPR16:$d), (ins memrm:$a), [], 6>;

def : Pat<(i16 (Mera400wrapper tglobaladdr:$g)), (MOVri tglobaladdr:$g)>;

// Address space casts: word address <-> byte address is a one bit shift.
let Constraints = "$d = $s", hasSideEffects = 0, Defs = [R0] in {
def SHL1 : Mera400Pseudo<(outs GPR16:$d), (ins GPR16:$s),
                         [(set GPR16:$d, (Mera400shl1 GPR16:$s))], 2>;
def SHR1 : Mera400Pseudo<(outs GPR16:$d), (ins GPR16:$s),
                         [(set GPR16:$d, (Mera400shr1 GPR16:$s))], 2>;
}

// Call frames are reserved in the function frame, so nothing is adjusted.
let Defs = [R7], Uses = [R7], hasSideEffects = 1 in {
def ADJCALLSTACKDOWN : Mera400Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                                     [(callseq_start timm:$amt1, timm:$amt2)],
                                     0>;
def ADJCALLSTACKUP : Mera400Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                                   [(callseq_end timm:$amt1, timm:$amt2)], 0>;
}

// "rj r4, callee" stores the return address in R4 and jumps. The callee
// changes the flags (R0).
let isCall = 1, Defs = [R0, R4], hasSideEffects = 1 in {
def CALL : Mera400Pseudo<(outs), (ins i16imm:$dst), [], 4>;
def CALLr : Mera400Pseudo<(outs), (ins GPR16:$dst), [(Mera400call GPR16:$dst)],
                          2>;
}
def : Pat<(Mera400call tglobaladdr:$dst), (CALL tglobaladdr:$dst)>;
def : Pat<(Mera400call texternalsym:$dst), (CALL texternalsym:$dst)>;

// Branches. The compare and its jumps are one pseudo instruction, so the flags
// in R0 never live across two machine instructions. BR_CC is expanded to
// "cw"/"cl"/"cwt" plus one or two jumps when it is printed; it is a
// conditional branch to $dst and falls through otherwise. The size field is
// the largest expansion; Mera400InstrInfo::getInstSizeInBytes is exact.
def brtarget : Operand<OtherVT>;

let isBranch = 1, isTerminator = 1 in {
def BR_CC : Mera400Pseudo<(outs), (ins GPR16:$a, GPR16:$b, i16imm:$cc,
                                       brtarget:$dst),
                          [(Mera400brcc GPR16:$a, GPR16:$b, timm:$cc, bb:$dst)],
                          10>;
def BR_CCI : Mera400Pseudo<(outs), (ins GPR16:$a, i16imm:$imm, i16imm:$cc,
                                        brtarget:$dst),
                           [(Mera400brcc GPR16:$a, imm:$imm, timm:$cc,
                                         bb:$dst)], 10>;
let isBarrier = 1 in {
// Short "ujs" when the destination is within reach, relaxed to JMPL otherwise.
def JMP : Mera400Pseudo<(outs), (ins brtarget:$dst), [(br bb:$dst)], 2>;
// Always the two-word "uj".
def JMPL : Mera400Pseudo<(outs), (ins brtarget:$dst), [], 4>;
}
}

// select and setcc become a small diamond of branches after instruction
// selection (Mera400TargetLowering::EmitInstrWithCustomInserter).
let usesCustomInserter = 1, hasSideEffects = 0 in {
def SELECT_CC : Mera400Pseudo<(outs GPR16:$d),
                              (ins GPR16:$a, GPR16:$b, i16imm:$cc, GPR16:$t,
                                   GPR16:$f),
                              [(set GPR16:$d,
                                    (Mera400selectcc GPR16:$a, GPR16:$b,
                                                     timm:$cc, GPR16:$t,
                                                     GPR16:$f))], 0>;
def SELECT_CCI : Mera400Pseudo<(outs GPR16:$d),
                               (ins GPR16:$a, i16imm:$imm, i16imm:$cc,
                                    GPR16:$t, GPR16:$f),
                               [(set GPR16:$d,
                                     (Mera400selectcc GPR16:$a, imm:$imm,
                                                      timm:$cc, GPR16:$t,
                                                      GPR16:$f))], 0>;
}

//===----------------------------------------------------------------------===//
// Wide integers, carries and shifts (code generation 4)
//===----------------------------------------------------------------------===//
//
// The carry of an addition is a 0/1 register value, never a live flag: every
// pseudo below turns the hardware carry flag (C) into a register or takes it
// from one inside its own expansion, so nothing can come between the
// instruction that sets C and the one that reads it.

// The nodes of the carry chain: value, carry (0 or 1), as in ISD.
def SDT_Mera400AddO : SDTypeProfile<2, 2, [SDTCisVT<0, i16>, SDTCisSameAs<1, 0>,
                                           SDTCisSameAs<2, 0>,
                                           SDTCisSameAs<3, 0>]>;
def SDT_Mera400AddCO : SDTypeProfile<2, 3, [SDTCisVT<0, i16>,
                                            SDTCisSameAs<1, 0>,
                                            SDTCisSameAs<2, 0>,
                                            SDTCisSameAs<3, 0>,
                                            SDTCisSameAs<4, 0>]>;
def Mera400uaddo : SDNode<"ISD::UADDO", SDT_Mera400AddO, [SDNPCommutative]>;
def Mera400usubo : SDNode<"ISD::USUBO", SDT_Mera400AddO>;
def Mera400uaddoc : SDNode<"ISD::UADDO_CARRY", SDT_Mera400AddCO>;
def Mera400usuboc : SDNode<"ISD::USUBO_CARRY", SDT_Mera400AddCO>;

// "$d = $s" and friends tie the operands the hardware overwrites; an output
// that is written before an input is read is early-clobber.
let hasSideEffects = 0, Defs = [R0] in {
// aw d, t ; lwt c, 0 ; ac c, c   (c = carry out: 0 + 0 + C)
let Constraints = "$d = $s", isCommutable = 1 in
def ADDO : Mera400Pseudo<(outs GPR16:$d, GPR16:$c), (ins GPR16:$s, GPR16:$t),
                         [(set GPR16:$d, GPR16:$c,
                               (Mera400uaddo GPR16:$s, GPR16:$t))], 6>;
// awt co, -1 (C = carry in: 0xFFFF + 1 carries) ; ac d, t ; lwt co, 0 ;
// ac co, co
let Constraints = "@earlyclobber $co, $d = $s, $co = $ci" in
def ADDCO : Mera400Pseudo<(outs GPR16:$d, GPR16:$co),
                          (ins GPR16:$s, GPR16:$t, GPR16:$ci),
                          [(set GPR16:$d, GPR16:$co,
                                (Mera400uaddoc GPR16:$s, GPR16:$t,
                                               GPR16:$ci))], 8>;
// sw sets C when there is no borrow, so the borrow is 1 - C:
// sw d, t ; lwt b, -1 ; ac b, b (0xFFFE + C) ; ngl b
let Constraints = "$d = $s" in
def SUBO : Mera400Pseudo<(outs GPR16:$d, GPR16:$b), (ins GPR16:$s, GPR16:$t),
                         [(set GPR16:$d, GPR16:$b,
                               (Mera400usubo GPR16:$s, GPR16:$t))], 8>;
// s - t - bi = ~(~s + t + bi), and ~s + t + bi carries exactly when there is a
// borrow: ngl d ; awt bo, -1 (C = bi) ; ac d, t ; ngl d ; lwt bo, 0 ; ac bo, bo
let Constraints = "@earlyclobber $bo, $d = $s, $bo = $bi" in
def SUBBO : Mera400Pseudo<(outs GPR16:$d, GPR16:$bo),
                          (ins GPR16:$s, GPR16:$t, GPR16:$bi),
                          [(set GPR16:$d, GPR16:$bo,
                                (Mera400usuboc GPR16:$s, GPR16:$t,
                                               GPR16:$bi))], 12>;

// 32-bit add and subtract without materializing the carry. Selected by
// Mera400ISelDAGToDAG when a carry is only used by the next limb.
// aw dl, bl ; ac dh, bh
let Constraints = "$dh = $ah, $dl = $al" in
def ADD32 : Mera400Pseudo<(outs GPR16:$dh, GPR16:$dl),
                          (ins GPR16:$ah, GPR16:$al, GPR16:$bh, GPR16:$bl),
                          [], 4>;
// a - b = a + ~b + 1 - borrow: sw dl, bl ; lw nt, bh ; ngl nt ; ac dh, nt
let Constraints = "$dh = $ah, $dl = $al" in
def SUB32 : Mera400Pseudo<(outs GPR16:$dh, GPR16:$dl, GPR16:$nt),
                          (ins GPR16:$ah, GPR16:$al, GPR16:$bh, GPR16:$bl),
                          [], 8>;
}

// Shifts. "slz", "srz" shift in a zero; the bit shifted out goes to Y, which
// "sly" and "sry" shift in. "sxu" copies the sign into X, "srx" shifts it in.
def SDT_Mera400Shift32 : SDTypeProfile<2, 2, [SDTCisVT<0, i16>,
                                              SDTCisSameAs<1, 0>,
                                              SDTCisSameAs<2, 0>,
                                              SDTCisSameAs<3, 0>]>;
def Mera400sra1 : SDNode<"Mera400ISD::SRA1", SDTIntUnaryOp>;
def Mera400signmask : SDNode<"Mera400ISD::SIGNMASK", SDTIntUnaryOp>;
def Mera400shl32 : SDNode<"Mera400ISD::SHL32", SDT_Mera400Shift32>;
def Mera400srl32 : SDNode<"Mera400ISD::SRL32", SDT_Mera400Shift32>;
def Mera400sra32 : SDNode<"Mera400ISD::SRA32", SDT_Mera400Shift32>;

let hasSideEffects = 0, Defs = [R0] in {
let Constraints = "$d = $s" in {
// sxu d ; srx d
def SRA1 : Mera400Pseudo<(outs GPR16:$d), (ins GPR16:$s),
                         [(set GPR16:$d, (Mera400sra1 GPR16:$s))], 4>;
// shc rotates right by the count (a negative count rotates left).
def SHCri : Mera400Pseudo<(outs GPR16:$d), (ins GPR16:$s, i16imm:$n), [], 2>;
}
// The sign bit as 0 or -1: sxu s (X = sign) ; lwt d, 0 ; slx d (d = X) ;
// nga d (d = -d)
def SIGNMASK : Mera400Pseudo<(outs GPR16:$d), (ins GPR16:$s),
                             [(set GPR16:$d, (Mera400signmask GPR16:$s))], 8>;
// slz dl ; sly dh
let Constraints = "$dh = $ah, $dl = $al" in {
def SHL32_1 : Mera400Pseudo<(outs GPR16:$dh, GPR16:$dl),
                            (ins GPR16:$ah, GPR16:$al),
                            [(set GPR16:$dh, GPR16:$dl,
                                  (Mera400shl32 GPR16:$ah, GPR16:$al))], 4>;
// srz dh ; sry dl
def SRL32_1 : Mera400Pseudo<(outs GPR16:$dh, GPR16:$dl),
                            (ins GPR16:$ah, GPR16:$al),
                            [(set GPR16:$dh, GPR16:$dl,
                                  (Mera400srl32 GPR16:$ah, GPR16:$al))], 4>;
// sxu dh ; srx dh ; sry dl
def SRA32_1 : Mera400Pseudo<(outs GPR16:$dh, GPR16:$dl),
                            (ins GPR16:$ah, GPR16:$al),
                            [(set GPR16:$dh, GPR16:$dl,
                                  (Mera400sra32 GPR16:$ah, GPR16:$al))], 6>;
}
}

def rotcount : ImmLeaf<i16, [{ return Imm >= 1 && Imm <= 15; }]>;
def NegRot : SDNodeXForm<imm, [{
  return CurDAG->getSignedTargetConstant(-N->getSExtValue(), SDLoc(N), MVT::i16);
}]>;
def : Pat<(rotr GPR16:$s, rotcount:$n), (SHCri GPR16:$s, imm:$n)>;
def : Pat<(rotl GPR16:$s, rotcount:$n), (SHCri GPR16:$s, (NegRot imm:$n))>;
def : Pat<(bswap GPR16:$s), (SHCri GPR16:$s, 8)>;

// Compare-and-branch and compare-and-select on 32 and 64-bit values: the limbs
// come high first. They are expanded after instruction selection into a chain
// of BR_CC (Mera400TargetLowering::EmitInstrWithCustomInserter).
def SDT_Mera400BrCC32 : SDTypeProfile<0, 6, [SDTCisVT<0, i16>, SDTCisVT<1, i16>,
                                             SDTCisVT<2, i16>, SDTCisVT<3, i16>,
                                             SDTCisVT<4, i16>,
                                             SDTCisVT<5, OtherVT>]>;
def SDT_Mera400BrCC64 : SDTypeProfile<0, 10, [SDTCisVT<0, i16>, SDTCisVT<1, i16>,
                                              SDTCisVT<2, i16>, SDTCisVT<3, i16>,
                                              SDTCisVT<4, i16>, SDTCisVT<5, i16>,
                                              SDTCisVT<6, i16>, SDTCisVT<7, i16>,
                                              SDTCisVT<8, i16>,
                                              SDTCisVT<9, OtherVT>]>;
def SDT_Mera400SelectCC32 : SDTypeProfile<1, 7, [SDTCisVT<0, i16>,
                                                 SDTCisVT<1, i16>, SDTCisVT<2, i16>,
                                                 SDTCisVT<3, i16>, SDTCisVT<4, i16>,
                                                 SDTCisVT<5, i16>,
                                                 SDTCisVT<6, i16>, SDTCisVT<7, i16>]>;
def SDT_Mera400SelectCC64 : SDTypeProfile<1, 11, [SDTCisVT<0, i16>,
                                                  SDTCisVT<1, i16>, SDTCisVT<2, i16>,
                                                  SDTCisVT<3, i16>, SDTCisVT<4, i16>,
                                                  SDTCisVT<5, i16>, SDTCisVT<6, i16>,
                                                  SDTCisVT<7, i16>, SDTCisVT<8, i16>,
                                                  SDTCisVT<9, i16>,
                                                  SDTCisVT<10, i16>, SDTCisVT<11, i16>]>;
def Mera400brcc32 : SDNode<"Mera400ISD::BR_CC32", SDT_Mera400BrCC32,
                           [SDNPHasChain]>;
def Mera400brcc64 : SDNode<"Mera400ISD::BR_CC64", SDT_Mera400BrCC64,
                           [SDNPHasChain]>;
def Mera400selectcc32 : SDNode<"Mera400ISD::SELECT_CC32",
                               SDT_Mera400SelectCC32>;
def Mera400selectcc64 : SDNode<"Mera400ISD::SELECT_CC64",
                               SDT_Mera400SelectCC64>;

let isBranch = 1, isTerminator = 1, usesCustomInserter = 1 in {
def BR_CC32 : Mera400Pseudo<(outs), (ins GPR16:$ah, GPR16:$al, GPR16:$bh,
                                         GPR16:$bl, i16imm:$cc, brtarget:$dst),
                            [(Mera400brcc32 GPR16:$ah, GPR16:$al, GPR16:$bh,
                                            GPR16:$bl, timm:$cc, bb:$dst)], 0>;
def BR_CC64 : Mera400Pseudo<(outs), (ins GPR16:$a0, GPR16:$a1, GPR16:$a2,
                                         GPR16:$a3, GPR16:$b0, GPR16:$b1,
                                         GPR16:$b2, GPR16:$b3, i16imm:$cc,
                                         brtarget:$dst),
                            [(Mera400brcc64 GPR16:$a0, GPR16:$a1, GPR16:$a2,
                                            GPR16:$a3, GPR16:$b0, GPR16:$b1,
                                            GPR16:$b2, GPR16:$b3, timm:$cc,
                                            bb:$dst)], 0>;
}
let usesCustomInserter = 1, hasSideEffects = 0 in {
def SELECT_CC32 : Mera400Pseudo<(outs GPR16:$d),
                                (ins GPR16:$ah, GPR16:$al, GPR16:$bh, GPR16:$bl,
                                     i16imm:$cc, GPR16:$t, GPR16:$f),
                                [(set GPR16:$d,
                                      (Mera400selectcc32 GPR16:$ah, GPR16:$al,
                                                         GPR16:$bh, GPR16:$bl,
                                                         timm:$cc, GPR16:$t,
                                                         GPR16:$f))], 0>;
def SELECT_CC64 : Mera400Pseudo<(outs GPR16:$d),
                                (ins GPR16:$a0, GPR16:$a1, GPR16:$a2, GPR16:$a3,
                                     GPR16:$b0, GPR16:$b1, GPR16:$b2, GPR16:$b3,
                                     i16imm:$cc, GPR16:$t, GPR16:$f),
                                [(set GPR16:$d,
                                      (Mera400selectcc64 GPR16:$a0, GPR16:$a1,
                                                         GPR16:$a2, GPR16:$a3,
                                                         GPR16:$b0, GPR16:$b1,
                                                         GPR16:$b2, GPR16:$b3,
                                                         timm:$cc, GPR16:$t,
                                                         GPR16:$f))], 0>;
}

// Floating point in the native 48-bit format (see Mera400FloatFormat.h). A
// value is three 16-bit words, the most significant first. The four basic
// operations are nodes with six inputs (operand 1, operand 2) and three
// results; a custom inserter turns them into AWP instructions (the optional
// arithmetic processor), which work on fixed registers R1:R2:R3:
//   store operand 2 to a stack temporary ; R1:R2:R3 = operand 1 ;
//   af/sf/mf/df temporary ; result = R1:R2:R3
// Without the AWP feature these nodes are not created; calls to the __m48_*
// helper functions are used instead.
def SDT_Mera400FOp48 : SDTypeProfile<3, 6, [SDTCisVT<0, i16>, SDTCisVT<1, i16>,
                                            SDTCisVT<2, i16>, SDTCisVT<3, i16>,
                                            SDTCisVT<4, i16>, SDTCisVT<5, i16>,
                                            SDTCisVT<6, i16>, SDTCisVT<7, i16>,
                                            SDTCisVT<8, i16>]>;
// "Negate the value if the word w is negative": three value words and w in,
// three words out. Used for fabs (w is the first word of the value) and
// copysign (w is the exclusive or of the first words of both operands).
def SDT_Mera400FNegIf : SDTypeProfile<3, 4, [SDTCisVT<0, i16>, SDTCisVT<1, i16>,
                                             SDTCisVT<2, i16>, SDTCisVT<3, i16>,
                                             SDTCisVT<4, i16>, SDTCisVT<5, i16>,
                                             SDTCisVT<6, i16>]>;
def Mera400fadd48 : SDNode<"Mera400ISD::FADD48", SDT_Mera400FOp48>;
def Mera400fsub48 : SDNode<"Mera400ISD::FSUB48", SDT_Mera400FOp48>;
def Mera400fmul48 : SDNode<"Mera400ISD::FMUL48", SDT_Mera400FOp48>;
def Mera400fdiv48 : SDNode<"Mera400ISD::FDIV48", SDT_Mera400FOp48>;
def Mera400fnegif : SDNode<"Mera400ISD::FNEGIF", SDT_Mera400FNegIf>;

class Mera400FOp48<SDNode Op>
    : Mera400Pseudo<(outs GPR16:$d0, GPR16:$d1, GPR16:$d2),
                    (ins GPR16:$a0, GPR16:$a1, GPR16:$a2, GPR16:$b0, GPR16:$b1,
                         GPR16:$b2),
                    [(set GPR16:$d0, GPR16:$d1, GPR16:$d2,
                          (Op GPR16:$a0, GPR16:$a1, GPR16:$a2, GPR16:$b0,
                              GPR16:$b1, GPR16:$b2))], 0>;
let usesCustomInserter = 1, hasSideEffects = 0 in {
def FADD48 : Mera400FOp48<Mera400fadd48>;
def FSUB48 : Mera400FOp48<Mera400fsub48>;
def FMUL48 : Mera400FOp48<Mera400fmul48>;
def FDIV48 : Mera400FOp48<Mera400fdiv48>;
// Expanded to a branch around a call to __m48_neg.
def FNEGIF48 : Mera400Pseudo<(outs GPR16:$d0, GPR16:$d1, GPR16:$d2),
                             (ins GPR16:$a0, GPR16:$a1, GPR16:$a2, GPR16:$w),
                             [(set GPR16:$d0, GPR16:$d1, GPR16:$d2,
                                   (Mera400fnegif GPR16:$a0, GPR16:$a1,
                                                  GPR16:$a2, GPR16:$w))], 0>;
}

// The AWP instructions themselves: the operand is the word address of the
// three-word memory operand, the other operand and the result are R1:R2:R3.
// They are only created by the custom inserter, never by selection.
let Defs = [R0, R1, R2, R3], Uses = [R1, R2, R3], mayLoad = 1,
    hasSideEffects = 1 in {
def AFrm : Mera400Pseudo<(outs), (ins memrm:$a), [], 4>;
def SFrm : Mera400Pseudo<(outs), (ins memrm:$a), [], 4>;
def MFrm : Mera400Pseudo<(outs), (ins memrm:$a), [], 4>;
def DFrm : Mera400Pseudo<(outs), (ins memrm:$a), [], 4>;
}
