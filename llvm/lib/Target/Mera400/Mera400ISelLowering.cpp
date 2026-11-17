//===-- Mera400ISelLowering.cpp - MERA-400 DAG lowering
//----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Turns LLVM IR operations into operations the MERA-400 can do. This runs on
// the selection DAG (directed acyclic graph) that instruction selection (ISel)
// builds for each basic block, before the DAG is matched to instructions.
// The machine has only 16-bit registers, so this file also decides how wider
// integers (kept as 16-bit "limbs", high word first), compares, shifts and
// calls are expressed. Anything the hardware cannot do directly becomes a
// library call or a diagnostic.
//
//===----------------------------------------------------------------------===//

#include "Mera400ISelLowering.h"
#include "Mera400.h"
#include "Mera400MachineFunctionInfo.h"
#include "Mera400SelectionDAGInfo.h"
#include "Mera400Subtarget.h"
#include "Mera400TargetMachine.h"
#include "MCTargetDesc/Mera400FloatFormat.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/GlobalValue.h"

using namespace llvm;

#define DEBUG_TYPE "mera400-lower"

#define GET_CALLING_CONV_IMPL
#include "Mera400GenCallingConv.inc"

Mera400TargetLowering::Mera400TargetLowering(const TargetMachine &TM,
                                             const Mera400Subtarget &STI)
    : TargetLowering(TM, STI) {
  // i16 is the only legal type; i32 and i64 are split into 16-bit parts by
  // type legalization.
  addRegisterClass(MVT::i16, &Mera400::GPR16RegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(Mera400::R7);
  // Boolean results are produced by branch diamonds that yield 0 or 1.
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);

  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);
  setOperationAction(ISD::ADDRSPACECAST, MVT::i16, Custom);
  // Rejected with a diagnostic: the frame has a fixed size (see
  // Mera400FrameLowering).
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Custom);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  // There is no sign-extending byte load or sign-extend instruction; both are
  // built from the zero-extending "lb" and arithmetic.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Custom);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i16, {MVT::i1, MVT::i8}, Custom);

  // Compares are fused with the branch or the select that uses them: the
  // compare instructions only set flags in R0, and a flag must not live
  // between two separately scheduled instructions. Wide compares become a
  // chain of branches (see EmitInstrWithCustomInserter).
  for (MVT VT : {MVT::i16, MVT::i32, MVT::i64}) {
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
  }
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::SELECT, MVT::i16, Expand);
  setOperationAction(ISD::SETCC, MVT::i16, Expand);
  setOperationAction(ISD::SETCC, {MVT::i32, MVT::i64}, Custom);
  setTargetDAGCombine({ISD::BRCOND, ISD::SELECT});

  // Floating point values are IEEE doubles in IR, but the machine format is
  // different (see Mera400FloatFormat.h), and f64 is not a legal type. The
  // generic type legalizer would turn every f64 operation into a call to an
  // IEEE soft-float function, which would silently give wrong results with
  // our format, so the operations are rewritten here, before type
  // legalization, into the native-format sequences or library calls.
  setTargetDAGCombine(
      {ISD::FADD, ISD::FSUB, ISD::FMUL, ISD::FDIV, ISD::FNEG, ISD::STORE,
       ISD::FABS, ISD::FCOPYSIGN, ISD::FREM, ISD::FSQRT, ISD::FSIN, ISD::FCOS,
       ISD::FTAN, ISD::FASIN, ISD::FACOS, ISD::FATAN, ISD::FATAN2, ISD::FSINH,
       ISD::FCOSH, ISD::FTANH, ISD::FPOW, ISD::FPOWI, ISD::FLDEXP, ISD::FEXP,
       ISD::FEXP2, ISD::FEXP10, ISD::FLOG, ISD::FLOG2, ISD::FLOG10,
       ISD::FFLOOR, ISD::FCEIL, ISD::FTRUNC, ISD::FRINT, ISD::FNEARBYINT,
       ISD::FROUND, ISD::FROUNDEVEN, ISD::FMINNUM, ISD::FMAXNUM, ISD::FMA,
       ISD::FP_TO_SINT, ISD::FP_TO_UINT, ISD::SINT_TO_FP, ISD::UINT_TO_FP,
       ISD::SETCC, ISD::BR_CC, ISD::SELECT_CC});

  // The carry of a 16-bit addition or subtraction is a 0 or 1 register value
  // (the ADDO/ADDCO/SUBO/SUBBO pseudos in Mera400InstrInfo.td), which is what
  // lets 32 and 64-bit arithmetic be built from 16-bit limbs.
  setOperationAction({ISD::UADDO, ISD::USUBO, ISD::UADDO_CARRY,
                      ISD::USUBO_CARRY},
                     MVT::i16, Legal);

  // Wide values are memory accessed in 16-bit words, high word first.
  setOperationAction({ISD::LOAD, ISD::STORE}, {MVT::i32, MVT::i64}, Custom);

  // Constant shifts and rotates are instructions; the rest are library calls
  // (the base CPU has no multiply or divide, which need the optional AWP
  // arithmetic unit, and no variable-count shift).
  setOperationAction({ISD::SHL, ISD::SRL, ISD::SRA}, {MVT::i16, MVT::i32},
                     Custom);
  setOperationAction({ISD::ROTL, ISD::ROTR}, MVT::i16, Custom);
  setOperationAction({ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS},
                     {MVT::i16, MVT::i32}, Expand);
  setOperationAction(ISD::BSWAP, MVT::i16, Legal);
  setOperationAction({ISD::MUL, ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM},
                     MVT::i16, LibCall);
  setOperationAction({ISD::SDIVREM, ISD::UDIVREM, ISD::SMUL_LOHI,
                      ISD::UMUL_LOHI, ISD::MULHS, ISD::MULHU},
                     MVT::i16, Expand);
  setOperationAction({ISD::CTPOP, ISD::CTLZ, ISD::CTTZ}, MVT::i16, Expand);
  // Switches are compare chains (no jump tables, and bit tests need shifts).
  // "indirectbr" and "blockaddress" are rejected before instruction selection
  // by Mera400LowerPointers.
  setMinimumJumpTableEntries(UINT_MAX);
  setMinimumBitTestCmps(UINT_MAX);
}

bool Mera400TargetLowering::areJTsAllowed(const Function *) const {
  return false;
}

// Reports a feature the backend does not implement as a diagnostic instead of
// crashing, so lowering can carry on with undefined values.
static void fail(const SDLoc &DL, SelectionDAG &DAG, const Twine &Msg) {
  DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
      DAG.getMachineFunction().getFunction(), Msg, DL.getDebugLoc()));
}

// Integers up to 64 bits arrive as 16-bit parts, high part first; wider ones
// and the argument kinds below are not supported.
template <typename ArgT>
static bool hasUnsupportedArg(const SmallVectorImpl<ArgT> &Args) {
  return llvm::any_of(Args, [](const ArgT &A) {
    return A.Flags.isByVal() || A.Flags.isInAlloca() || A.Flags.isNest() ||
           A.ArgVT.getSizeInBits() > 64;
  });
}

// A floating point value is an f64 for the type legalizer, which splits it
// into four 16-bit parts, but only the first three carry the native value (the
// fourth word of the memory format is padding). Parts of one value are
// consecutive in the argument list, so the fourth of each group of four is the
// padding: it is neither passed nor returned, and the receiving side makes it
// zero.
template <typename ArgT>
static bool isFloatPadding(const SmallVectorImpl<ArgT> &Args, unsigned I) {
  if (Args[I].ArgVT != MVT::f64)
    return false;
  unsigned Position = 0;
  while (Position < I && Args[I - Position - 1].ArgVT == MVT::f64)
    ++Position;
  return Position % 4 == 3;
}

template <typename ArgT>
static SmallVector<ArgT, 8> dropFloatPadding(const SmallVectorImpl<ArgT> &Args) {
  SmallVector<ArgT, 8> Parts;
  for (unsigned I = 0, E = Args.size(); I != E; ++I)
    if (!isFloatPadding(Args, I))
      Parts.push_back(Args[I]);
  return Parts;
}

// Like CCState::AnalyzeFormalArguments and AnalyzeCallOperands, with one
// rule added: a 32-bit value that would be split between R3 and the stack
// goes to the stack as a whole, so that it is never torn between a register
// and memory. The same holds for the three words of a floating point value.
// Padding parts must have been dropped.
template <typename ArgT>
static void analyzeArgs(CCState &CCInfo, const SmallVectorImpl<ArgT> &Args) {
  static const MCPhysReg Regs[] = {Mera400::R1, Mera400::R2, Mera400::R3};
  for (unsigned I = 0, E = Args.size(); I != E; ++I) {
    const ArgT &A = Args[I];
    unsigned Next = CCInfo.getFirstUnallocated(Regs);
    bool Torn = A.Flags.isSplit() &&
                ((A.ArgVT.getSizeInBits() == 32 && Next == 2) ||
                 (A.ArgVT == MVT::f64 && Next != 0 && Next < 3));
    if (Torn)
      for (unsigned R = Next; R != 3; ++R)
        CCInfo.AllocateReg(Regs[R]);
    if (CC_Mera400(I, A.VT, A.VT, CCValAssign::Full, A.Flags, A.OrigTy,
                   CCInfo))
      report_fatal_error("unable to allocate argument #" + Twine(I));
  }
}

// Incoming arguments: R1..R3 are copied into virtual registers; the rest are
// loaded from fixed stack slots the caller filled.
SDValue Mera400TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  SmallVector<CCValAssign, 4> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  SmallVector<ISD::InputArg, 8> Parts = dropFloatPadding(Ins);
  if (!hasUnsupportedArg(Ins))
    analyzeArgs(CCInfo, Parts);

  // Variadic arguments are all passed on the stack, starting at the first
  // stack word the named arguments did not use; va_start needs its address.
  if (IsVarArg) {
    int FI = MF.getFrameInfo().CreateFixedObject(2, CCInfo.getStackSize(), true);
    MF.getInfo<Mera400MachineFunctionInfo>()->setVarArgsFrameIndex(FI);
  }

  bool Supported =
      !hasUnsupportedArg(Ins) &&
      llvm::all_of(ArgLocs, [](const CCValAssign &VA) {
        return VA.getLocInfo() == CCValAssign::Full;
      });
  if (!Supported) {
    fail(DL, DAG,
         "Mera400: this kind of argument (integer wider than 64 bits, byval, "
         "inalloca or nest) is not supported");
    for (const ISD::InputArg &In : Ins)
      InVals.push_back(DAG.getUNDEF(In.VT));
    return Chain;
  }

  MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<SDValue, 8> Received;
  for (const CCValAssign &VA : ArgLocs) {
    if (VA.isRegLoc()) {
      Register VReg = MRI.createVirtualRegister(&Mera400::GPR16RegClass);
      MRI.addLiveIn(VA.getLocReg(), VReg);
      Received.push_back(DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT()));
      continue;
    }
    // Stack arguments start at the stack pointer the caller had at the call.
    // CCValAssign offsets are in octets; the object is one 16-bit word.
    int FI = MFI.CreateFixedObject(2, VA.getLocMemOffset(), true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
    Received.push_back(DAG.getLoad(VA.getLocVT(), DL, Chain, FIN,
                                   MachinePointerInfo::getFixedStack(MF, FI)));
  }
  unsigned Next = 0;
  for (unsigned I = 0, E = Ins.size(); I != E; ++I)
    InVals.push_back(isFloatPadding(Ins, I) ? DAG.getConstant(0, DL, MVT::i16)
                                            : Received[Next++]);
  return Chain;
}

// Copies the values a call returns out of R1..R3, chained by glue so that
// nothing is scheduled between the call and the copies.
SDValue Mera400TargetLowering::LowerCallResult(
    SDValue Chain, SDValue Glue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(dropFloatPadding(Ins), RetCC_Mera400);
  unsigned Next = 0;
  for (unsigned I = 0, E = Ins.size(); I != E; ++I) {
    if (isFloatPadding(Ins, I)) {
      InVals.push_back(DAG.getConstant(0, DL, MVT::i16));
      continue;
    }
    const CCValAssign &VA = RVLocs[Next++];
    Chain = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getValVT(), Glue)
                .getValue(1);
    Glue = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }
  return Chain;
}

// Outgoing calls: arguments go to R1..R3 or into the outgoing area at the
// bottom of the caller's frame (R7 + word offset), then CALL ("rj r4, callee")
// stores the return address in R4. The callee-preserved mask tells the
// register allocator that only R5 and R6 survive.
SDValue
Mera400TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                 SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();

  auto Unsupported = [&](const Twine &Msg) {
    fail(DL, DAG, Msg);
    for (const ISD::InputArg &In : CLI.Ins)
      InVals.push_back(DAG.getUNDEF(In.VT));
    return Chain;
  };

  bool MustTail = CLI.CB && CLI.CB->isMustTailCall();
  CLI.IsTailCall = false;
  if (MustTail)
    return Unsupported("Mera400: tail calls are not supported");
  if (hasUnsupportedArg(CLI.Outs) || hasUnsupportedArg(CLI.Ins))
    return Unsupported("Mera400: this kind of argument (integer wider than 64 "
                       "bits, byval, inalloca or nest) is not supported");

  SmallVector<CCValAssign, 8> ArgLocs;
  CCState CCInfo(CLI.CallConv, CLI.IsVarArg, MF, ArgLocs, *DAG.getContext());
  SmallVector<ISD::OutputArg, 8> OutParts = dropFloatPadding(CLI.Outs);
  SmallVector<SDValue, 8> OutVals;
  for (unsigned I = 0, E = CLI.Outs.size(); I != E; ++I)
    if (!isFloatPadding(CLI.Outs, I))
      OutVals.push_back(CLI.OutVals[I]);
  // The named arguments take registers as usual. Every variadic argument goes
  // on the stack, one word per 16-bit part, after the named ones: the callee
  // cannot know how many there are, so it needs no register-save code, and
  // va_start is simply the address of the first such stack word.
  unsigned NumFixed = 0;
  while (NumFixed != OutParts.size() && !OutParts[NumFixed].Flags.isVarArg())
    ++NumFixed;
  analyzeArgs(CCInfo, SmallVector<ISD::OutputArg, 8>(
                          OutParts.begin(), OutParts.begin() + NumFixed));
  for (unsigned I = NumFixed, E = OutParts.size(); I != E; ++I) {
    unsigned Offset = CCInfo.AllocateStack(2, Align(2));
    CCInfo.addLoc(CCValAssign::getMem(I, OutParts[I].VT, Offset,
                                      OutParts[I].VT, CCValAssign::Full));
  }
  if (llvm::any_of(ArgLocs, [](const CCValAssign &VA) {
        return VA.getLocInfo() != CCValAssign::Full;
      }))
    return Unsupported("Mera400: this kind of argument is not yet supported");

  unsigned NumBytes = CCInfo.getStackSize();
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  SmallVector<std::pair<unsigned, SDValue>, 3> RegsToPass;
  SmallVector<SDValue, 8> MemOps;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    SDValue Arg = OutVals[I];
    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), Arg});
      continue;
    }
    // The outgoing area is at the bottom of the frame: argument K is at R7+K.
    // The location offset is in octets and a word pointer counts words, hence
    // the division by two.
    unsigned Offset = VA.getLocMemOffset();
    SDValue Addr = DAG.getNode(ISD::ADD, DL, MVT::i16,
                               DAG.getRegister(Mera400::R7, MVT::i16),
                               DAG.getConstant(Offset / 2, DL, MVT::i16));
    MemOps.push_back(
        DAG.getStore(Chain, DL, Arg, Addr,
                     MachinePointerInfo::getStack(MF, Offset)));
  }
  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOps);

  SDValue Glue;
  for (auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg.first, Reg.second, Glue);
    Glue = Chain.getValue(1);
  }

  // A callee reached through a large model byte pointer (a function pointer,
  // a virtual table slot or a pointer to member function: all 32-bit byte
  // addresses, see Mera400LowerGlobalSpaces.cpp) is 32 bits wide; the call
  // needs the word address, the byte address halved.
  if (Callee.getValueType() == MVT::i32)
    Callee = DAG.getNode(
        ISD::TRUNCATE, DL, MVT::i16,
        DAG.getNode(ISD::SRL, DL, MVT::i32, Callee,
                    DAG.getConstant(1, DL, MVT::i32)));

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16,
                                        G->getOffset());
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16);

  SmallVector<SDValue, 8> Ops = {Chain, Callee};
  for (auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  Ops.push_back(DAG.getRegisterMask(TRI->getCallPreservedMask(MF, CLI.CallConv)));
  if (Glue.getNode())
    Ops.push_back(Glue);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(Mera400ISD::CALL, DL, NodeTys, Ops);
  Glue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);
  return LowerCallResult(Chain, Glue, CLI.CallConv, CLI.IsVarArg, CLI.Ins, DL,
                         DAG, InVals);
}

// The address of a global is a 16-bit immediate that the assembler resolves
// with a WORD16 relocation (a word address); WRAPPER keeps the target node
// from being folded into other arithmetic.
SDValue Mera400TargetLowering::LowerGlobalAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  auto *N = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(Op);
  SDValue TGA =
      DAG.getTargetGlobalAddress(N->getGlobal(), DL, MVT::i16, N->getOffset());
  return DAG.getNode(Mera400ISD::WRAPPER, DL, MVT::i16, TGA);
}

// Address space 0 holds word addresses and address space 1 byte addresses. A
// word address becomes a byte address by shifting left one bit; the other
// direction drops the octet selector (the lowest bit).
SDValue Mera400TargetLowering::LowerAddrSpaceCast(SDValue Op,
                                                  SelectionDAG &DAG) const {
  auto *N = cast<AddrSpaceCastSDNode>(Op);
  SDLoc DL(Op);
  unsigned From = N->getSrcAddressSpace(), To = N->getDestAddressSpace();
  if (From == To)
    return N->getOperand(0);
  if (From == 0 && To == 1)
    return DAG.getNode(Mera400ISD::SHL1, DL, MVT::i16, N->getOperand(0));
  if (From == 1 && To == 0)
    return DAG.getNode(Mera400ISD::SHR1, DL, MVT::i16, N->getOperand(0));
  fail(DL, DAG, "Mera400: unsupported address space cast");
  return DAG.getUNDEF(MVT::i16);
}

// va_start stores into the va_list object the byte address of the first
// variadic argument slot. A va_list is a byte pointer (16 bits in the small
// model, 32 in the large one), like every C data pointer. The slot's frame
// index is a word address, so it is converted the same way as an
// addrspacecast from address space 0 to 1: shifted left one bit (in the large
// model first widened, so that a word address above 0x7fff does not
// overflow). Only va_start needs the DAG: va_arg, va_copy and va_end are
// expanded into plain loads and stores by Mera400LowerPointers.
SDValue Mera400TargetLowering::LowerVASTART(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MachineFunction &MF = DAG.getMachineFunction();
  int FI = MF.getInfo<Mera400MachineFunctionInfo>()->getVarArgsFrameIndex();
  SDValue Word = DAG.getFrameIndex(FI, MVT::i16);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  if (DAG.getDataLayout().getPointerSizeInBits(1) == 32) {
    // The byte address is 32 bits, but i32 is no longer a legal type here
    // (VASTART is legalized after the types), so the two words are built and
    // stored separately: the high word first, like every 32-bit value.
    SDValue Low = DAG.getNode(Mera400ISD::SHL1, DL, MVT::i16, Word);
    SDValue High = DAG.getNode(ISD::SRL, DL, MVT::i16, Word,
                               DAG.getConstant(15, DL, MVT::i16));
    SDValue Chain = DAG.getStore(Op.getOperand(0), DL, High, Op.getOperand(1),
                                 MachinePointerInfo(SV));
    SDValue LowAddr = DAG.getNode(ISD::ADD, DL, MVT::i16, Op.getOperand(1),
                                  DAG.getConstant(1, DL, MVT::i16));
    // The pointer info offset is in bytes (2 per word), which keeps the
    // alignment of the second store at one word.
    return DAG.getStore(Chain, DL, Low, LowAddr, MachinePointerInfo(SV, 2));
  }
  SDValue Byte = DAG.getNode(Mera400ISD::SHL1, DL, MVT::i16, Word);
  return DAG.getStore(Op.getOperand(0), DL, Byte, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

// Maps an LLVM integer predicate to the backend's condition numbering.
static Mera400::CondCode getCondCode(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETEQ:
    return Mera400::CC_EQ;
  case ISD::SETNE:
    return Mera400::CC_NE;
  case ISD::SETLT:
    return Mera400::CC_LT;
  case ISD::SETGE:
    return Mera400::CC_GE;
  case ISD::SETGT:
    return Mera400::CC_GT;
  case ISD::SETLE:
    return Mera400::CC_LE;
  case ISD::SETULT:
    return Mera400::CC_ULT;
  case ISD::SETUGE:
    return Mera400::CC_UGE;
  case ISD::SETUGT:
    return Mera400::CC_UGT;
  case ISD::SETULE:
    return Mera400::CC_ULE;
  default:
    llvm_unreachable("integer compares only");
  }
}

// The hardware compares a register with a register or a constant argument, so
// a constant goes on the right.
static SDValue getCondCodeOperand(SDValue &LHS, SDValue &RHS,
                                  ISD::CondCode CC, const SDLoc &DL,
                                  SelectionDAG &DAG) {
  if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }
  return DAG.getTargetConstant(getCondCode(CC), DL, MVT::i16);
}

// The 16-bit limbs of an integer, the highest first (in memory, in registers
// and in the compare chains).
static void splitLimbs(SDValue V, SmallVectorImpl<SDValue> &Limbs,
                       const SDLoc &DL, SelectionDAG &DAG) {
  EVT VT = V.getValueType();
  if (VT == MVT::i16) {
    Limbs.push_back(V);
    return;
  }
  EVT Half = VT == MVT::i64 ? MVT::i32 : MVT::i16;
  SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, Half, V,
                           DAG.getIntPtrConstant(1, DL));
  SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, Half, V,
                           DAG.getIntPtrConstant(0, DL));
  splitLimbs(Hi, Limbs, DL, DAG);
  splitLimbs(Lo, Limbs, DL, DAG);
}

// True for i32 and i64, the integers that do not fit one register.
static bool isWideInt(EVT VT) {
  return VT.isScalarInteger() && VT.getSizeInBits() > 16;
}

// A conditional branch becomes one fused BR_CC node carrying both compare
// operands. Wide operands use BR_CC32/BR_CC64, which are expanded into a chain
// of 16-bit BR_CC blocks later because the hardware compares one word at a
// time.
SDValue Mera400TargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(2), RHS = Op.getOperand(3);
  ISD::CondCode ISDCC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  if (isWideInt(LHS.getValueType())) {
    bool Is64 = LHS.getValueType() == MVT::i64;
    SmallVector<SDValue, 12> Ops = {Op.getOperand(0)};
    splitLimbs(LHS, Ops, DL, DAG);
    splitLimbs(RHS, Ops, DL, DAG);
    Ops.push_back(DAG.getTargetConstant(getCondCode(ISDCC), DL, MVT::i16));
    Ops.push_back(Op.getOperand(4));
    return DAG.getNode(Is64 ? Mera400ISD::BR_CC64 : Mera400ISD::BR_CC32, DL,
                       MVT::Other, Ops);
  }
  SDValue CC = getCondCodeOperand(LHS, RHS, ISDCC, DL, DAG);
  return DAG.getNode(Mera400ISD::BR_CC, DL, MVT::Other, Op.getOperand(0), LHS,
                     RHS, CC, Op.getOperand(4));
}

// A select on wide operands (with a 16-bit result) or a wide compare used as
// a value (SETCC, selecting 1 or 0).
static SDValue getWideSelectCC(const SDLoc &DL, SelectionDAG &DAG, SDValue LHS,
                               SDValue RHS, ISD::CondCode CC, SDValue T,
                               SDValue F) {
  bool Is64 = LHS.getValueType() == MVT::i64;
  SmallVector<SDValue, 12> Ops;
  splitLimbs(LHS, Ops, DL, DAG);
  splitLimbs(RHS, Ops, DL, DAG);
  Ops.push_back(DAG.getTargetConstant(getCondCode(CC), DL, MVT::i16));
  Ops.push_back(T);
  Ops.push_back(F);
  return DAG.getNode(Is64 ? Mera400ISD::SELECT_CC64 : Mera400ISD::SELECT_CC32,
                     DL, MVT::i16, Ops);
}

SDValue Mera400TargetLowering::LowerSELECT_CC(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);
  ISD::CondCode ISDCC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  if (isWideInt(LHS.getValueType()))
    return getWideSelectCC(DL, DAG, LHS, RHS, ISDCC, Op.getOperand(2),
                           Op.getOperand(3));
  SDValue CC = getCondCodeOperand(LHS, RHS, ISDCC, DL, DAG);
  return DAG.getNode(Mera400ISD::SELECT_CC, DL, MVT::i16, LHS, RHS, CC,
                     Op.getOperand(2), Op.getOperand(3));
}

SDValue Mera400TargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return getWideSelectCC(DL, DAG, Op.getOperand(0), Op.getOperand(1),
                         cast<CondCodeSDNode>(Op.getOperand(2))->get(),
                         DAG.getConstant(1, DL, MVT::i16),
                         DAG.getConstant(0, DL, MVT::i16));
}

// Applies the one-bit operation Opc Count times.
static SDValue repeatNode(unsigned Opc, SDValue X, unsigned Count,
                          const SDLoc &DL, SelectionDAG &DAG) {
  for (unsigned I = 0; I != Count; ++I)
    X = DAG.getNode(Opc, DL, MVT::i16, X);
  return X;
}

// 16-bit shifts. A shift by 1 or 2 is that many one-bit shifts. Shifts by
// 3..15 rotate and clear the bits that wrapped around ("shc" rotates right by
// its count, so a left shift by n rotates right by 16-n, written as -n). An
// arithmetic shift by 15 is the sign mask. The rest, including every variable
// amount, is a call to the compiler-rt routine.
SDValue Mera400TargetLowering::LowerShift(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  unsigned Opc = Op.getOpcode();
  SDValue X = Op.getOperand(0), Amt = Op.getOperand(1);
  if (auto *C = dyn_cast<ConstantSDNode>(Amt)) {
    uint64_t N = C->getZExtValue();
    if (N == 0)
      return X;
    if (N >= 16)
      return DAG.getUNDEF(MVT::i16);
    if (Opc == ISD::SHL) {
      if (N <= 2)
        return repeatNode(Mera400ISD::SHL1, X, N, DL, DAG);
      SDValue R = DAG.getNode(ISD::ROTL, DL, MVT::i16, X, Amt);
      return DAG.getNode(ISD::AND, DL, MVT::i16, R,
                         DAG.getConstant((0xFFFFu << N) & 0xFFFF, DL,
                                         MVT::i16));
    }
    if (Opc == ISD::SRL) {
      if (N <= 2)
        return repeatNode(Mera400ISD::SHR1, X, N, DL, DAG);
      SDValue R = DAG.getNode(ISD::ROTR, DL, MVT::i16, X, Amt);
      return DAG.getNode(ISD::AND, DL, MVT::i16, R,
                         DAG.getConstant(0xFFFFu >> N, DL, MVT::i16));
    }
    if (N <= 2)
      return repeatNode(Mera400ISD::SRA1, X, N, DL, DAG);
    if (N == 15)
      return DAG.getNode(Mera400ISD::SIGNMASK, DL, MVT::i16, X);
  }
  RTLIB::Libcall LC = Opc == ISD::SHL   ? RTLIB::SHL_I16
                      : Opc == ISD::SRL ? RTLIB::SRL_I16
                                        : RTLIB::SRA_I16;
  TargetLowering::MakeLibCallOptions CallOptions;
  return makeLibCall(DAG, LC, MVT::i16, {X, Amt}, CallOptions, DL,
                     DAG.getEntryNode())
      .first;
}

// Constant rotates by 1..15 are the single "shc" instruction (rotate right by
// the count; SHCri uses a negative count for a left rotate). Others expand.
SDValue Mera400TargetLowering::LowerRotate(SDValue Op,
                                           SelectionDAG &DAG) const {
  if (auto *C = dyn_cast<ConstantSDNode>(Op.getOperand(1))) {
    uint64_t N = C->getZExtValue();
    if (N >= 1 && N <= 15)
      return Op;
    if (N == 0)
      return Op.getOperand(0);
  }
  return expandROT(Op.getNode(), false, DAG);
}

// Sign extension of a byte: (x ^ 0x80) - 0x80 once the bits above the byte
// are known to be zero ("xr" with 0x80 flips the sign bit of the byte, the
// subtraction then moves it into all the upper bits).
static SDValue signExtendByte(SDValue X, const SDLoc &DL, SelectionDAG &DAG) {
  if (!DAG.MaskedValueIsZero(X, APInt(16, 0xFF00)))
    X = DAG.getNode(ISD::AND, DL, MVT::i16, X, DAG.getConstant(0xFF, DL,
                                                                MVT::i16));
  X = DAG.getNode(ISD::XOR, DL, MVT::i16, X, DAG.getConstant(0x80, DL,
                                                              MVT::i16));
  return DAG.getNode(ISD::ADD, DL, MVT::i16, X,
                     DAG.getConstant(0xFF80, DL, MVT::i16));
}

SDValue
Mera400TargetLowering::LowerSignExtendInReg(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  // A 1-bit value is 0 or 1; its sign extension is 0 or -1, which is the
  // negation of the masked value.
  if (cast<VTSDNode>(Op.getOperand(1))->getVT() == MVT::i1) {
    SDValue Bit = DAG.getNode(ISD::AND, DL, MVT::i16, Op.getOperand(0),
                              DAG.getConstant(1, DL, MVT::i16));
    return DAG.getNode(ISD::SUB, DL, MVT::i16,
                       DAG.getConstant(0, DL, MVT::i16), Bit);
  }
  return signExtendByte(Op.getOperand(0), DL, DAG);
}

// A sign-extending load of a byte (or bit) is the zero-extending one (the
// "lb" instruction) followed by the same sign extension.
SDValue Mera400TargetLowering::LowerLoad(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  auto *LD = cast<LoadSDNode>(Op);
  SDValue Zext = DAG.getExtLoad(ISD::ZEXTLOAD, DL, MVT::i16, LD->getChain(),
                                LD->getBasePtr(), MVT::i8,
                                LD->getMemOperand());
  SDValue Val;
  if (LD->getMemoryVT() == MVT::i1) {
    SDValue Bit = DAG.getNode(ISD::AND, DL, MVT::i16, Zext,
                              DAG.getConstant(1, DL, MVT::i16));
    Val = DAG.getNode(ISD::SUB, DL, MVT::i16, DAG.getConstant(0, DL, MVT::i16),
                      Bit);
  } else {
    Val = signExtendByte(Zext, DL, DAG);
  }
  return DAG.getMergeValues({Val, Zext.getValue(1)}, DL);
}

// Wide values are stored as 16-bit words, the high word at the lowest address.
// A word pointer counts words, so the next word is the pointer plus one.
SDValue Mera400TargetLowering::LowerWideStore(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  auto *ST = cast<StoreSDNode>(Op);
  if (ST->isTruncatingStore() || ST->getAddressSpace() != 0)
    return SDValue();
  SmallVector<SDValue, 4> Limbs;
  splitLimbs(ST->getValue(), Limbs, DL, DAG);
  SmallVector<SDValue, 4> Stores;
  for (unsigned I = 0, E = Limbs.size(); I != E; ++I) {
    SDValue Addr = DAG.getNode(ISD::ADD, DL, MVT::i16, ST->getBasePtr(),
                               DAG.getConstant(I, DL, MVT::i16));
    Stores.push_back(DAG.getStore(
        ST->getChain(), DL, Limbs[I], Addr,
        ST->getPointerInfo().getWithOffset(2 * I), Align(2),
        ST->getMemOperand()->getFlags(), ST->getAAInfo()));
  }
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
}

// Custom type legalization of wide results: a wide load becomes 16-bit word
// loads at consecutive word addresses (high word first, then BUILD_PAIR joins
// them), and a 32-bit shift by 1 stays inline.
void Mera400TargetLowering::ReplaceNodeResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  switch (N->getOpcode()) {
  case ISD::LOAD: {
    auto *LD = cast<LoadSDNode>(N);
    if (LD->getExtensionType() != ISD::NON_EXTLOAD || LD->getAddressSpace() != 0)
      return;
    unsigned Words = VT.getSizeInBits() / 16;
    SmallVector<SDValue, 4> Vals, Chains;
    for (unsigned I = 0; I != Words; ++I) {
      SDValue Addr = DAG.getNode(ISD::ADD, DL, MVT::i16, LD->getBasePtr(),
                                 DAG.getConstant(I, DL, MVT::i16));
      SDValue W = DAG.getLoad(MVT::i16, DL, LD->getChain(), Addr,
                              LD->getPointerInfo().getWithOffset(2 * I),
                              Align(2), LD->getMemOperand()->getFlags(),
                              LD->getAAInfo());
      Vals.push_back(W);
      Chains.push_back(W.getValue(1));
    }
    SDValue Res;
    if (Words == 2) {
      Res = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, Vals[1], Vals[0]);
    } else {
      SDValue Hi = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, Vals[1], Vals[0]);
      SDValue Lo = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, Vals[3], Vals[2]);
      Res = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Lo, Hi);
    }
    Results.push_back(Res);
    Results.push_back(DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains));
    return;
  }
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA: {
    // Only a 32-bit shift by 1 is done inline (carrying the bit through Y or
    // X); the other shifts are split into 16-bit shifts or are library calls.
    auto *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (VT != MVT::i32 || !C || C->getZExtValue() != 1)
      return;
    unsigned Opc = N->getOpcode() == ISD::SHL   ? Mera400ISD::SHL32
                   : N->getOpcode() == ISD::SRL ? Mera400ISD::SRL32
                                                : Mera400ISD::SRA32;
    SmallVector<SDValue, 2> Limbs;
    splitLimbs(N->getOperand(0), Limbs, DL, DAG);
    SDValue R = DAG.getNode(Opc, DL, DAG.getVTList(MVT::i16, MVT::i16),
                            Limbs[0], Limbs[1]);
    Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32,
                                  R.getValue(1), R.getValue(0)));
    return;
  }
  case ISD::SELECT_CC: {
    // Compute the condition once, then select each limb with it.
    SDValue Cond = DAG.getSetCC(DL, MVT::i16, N->getOperand(0),
                                N->getOperand(1),
                                cast<CondCodeSDNode>(N->getOperand(4))->get());
    Results.push_back(
        DAG.getSelect(DL, VT, Cond, N->getOperand(2), N->getOperand(3)));
    return;
  }
  default:
    return;
  }
}

//===----------------------------------------------------------------------===//
// Floating point. IR has IEEE doubles, the machine has the native 48-bit
// format (Mera400FloatFormat.h), kept in four words of which three are
// significant. Everything below runs before type legalization, while values
// are still f64.
//===----------------------------------------------------------------------===//

// The three significant words of a floating point value, the first holding
// the sign and the top of the mantissa. The f64 is viewed as an i64, which
// legalization then splits into four words (the last is padding).
static void splitFloat(SDValue V, SmallVectorImpl<SDValue> &Words,
                       const SDLoc &DL, SelectionDAG &DAG) {
  SmallVector<SDValue, 4> Limbs;
  splitLimbs(DAG.getNode(ISD::BITCAST, DL, MVT::i64, V), Limbs, DL, DAG);
  Words.append(Limbs.begin(), Limbs.begin() + 3);
}

// The inverse of splitFloat; the padding word is made zero.
static SDValue joinFloat(SDValue W0, SDValue W1, SDValue W2, const SDLoc &DL,
                         SelectionDAG &DAG) {
  SDValue Hi = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, W1, W0);
  SDValue Lo = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32,
                           DAG.getConstant(0, DL, MVT::i16), W2);
  return DAG.getNode(ISD::BITCAST, DL, MVT::f64,
                     DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Lo, Hi));
}

// A floating point constant is converted to the native format here, at
// compile time. It becomes an integer constant reinterpreted as f64; the
// constant is marked opaque so that the DAG does not fold the reinterpretation
// back into an (IEEE) floating point constant.
static SDValue lowerFPConstant(ConstantFPSDNode *C, SelectionDAG &DAG,
                               const SDLoc &DL) {
  uint16_t Words[3] = {0, 0, 0};
  double Value = C->getValueAPF().convertToDouble();
  Mera400::FloatConvStatus Status =
      Mera400::convertDoubleToNativeFloat(Value, /*RoundToNearest=*/true,
                                          Words);
  switch (Status) {
  case Mera400::FloatConvStatus::Ok:
    break;
  case Mera400::FloatConvStatus::Underflow:
    // Too small to represent: it flushes to zero.
    Words[0] = Words[1] = Words[2] = 0;
    break;
  case Mera400::FloatConvStatus::Overflow:
  case Mera400::FloatConvStatus::NotANumber:
    SmallString<32> Text;
    C->getValueAPF().toString(Text);
    fail(DL, DAG,
         "Mera400: the floating point constant " + Twine(Text) +
             " is not representable in the MERA-400 floating point format "
             "(infinity, NaN, or outside about 1e-38 .. 1e38)");
    Words[0] = Words[1] = Words[2] = 0;
    break;
  }
  uint64_t Bits = (uint64_t(Words[0]) << 48) | (uint64_t(Words[1]) << 32) |
                  (uint64_t(Words[2]) << 16);
  return DAG.getNode(ISD::BITCAST, DL, MVT::f64,
                     DAG.getConstant(Bits, DL, MVT::i64, /*isTarget=*/false,
                                     /*isOpaque=*/true));
}

// An operand that is a floating point constant, converted to the native
// format; any other operand is returned unchanged. The elements of an
// aggregate (a structure or array) value arrive as results of MERGE_VALUES
// nodes, which the DAG combiner removes only later, so look through them.
static SDValue nativeOperand(SDValue V, SelectionDAG &DAG) {
  SDValue Elt = V;
  while (Elt.getOpcode() == ISD::MERGE_VALUES)
    Elt = Elt.getOperand(Elt.getResNo());
  if (auto *C = dyn_cast<ConstantFPSDNode>(Elt))
    if (Elt.getValueType() == MVT::f64)
      return lowerFPConstant(C, DAG, SDLoc(V));
  return V;
}

// The C library function that implements a floating point operation without
// a native instruction; its double is the native format.
static const char *getMathFunction(unsigned Opcode) {
  switch (Opcode) {
  case ISD::FREM:
    return "fmod";
  case ISD::FSQRT:
    return "sqrt";
  case ISD::FSIN:
    return "sin";
  case ISD::FCOS:
    return "cos";
  case ISD::FTAN:
    return "tan";
  case ISD::FASIN:
    return "asin";
  case ISD::FACOS:
    return "acos";
  case ISD::FATAN:
    return "atan";
  case ISD::FATAN2:
    return "atan2";
  case ISD::FSINH:
    return "sinh";
  case ISD::FCOSH:
    return "cosh";
  case ISD::FTANH:
    return "tanh";
  case ISD::FPOW:
    return "pow";
  // There is no standard C function for llvm.powi (a power with an integer
  // exponent), so it gets a helper name like the other __m48_ functions.
  case ISD::FPOWI:
    return "__m48_powi";
  case ISD::FLDEXP:
    return "ldexp";
  case ISD::FEXP:
    return "exp";
  case ISD::FEXP2:
    return "exp2";
  case ISD::FEXP10:
    return "exp10";
  case ISD::FLOG:
    return "log";
  case ISD::FLOG2:
    return "log2";
  case ISD::FLOG10:
    return "log10";
  case ISD::FFLOOR:
    return "floor";
  case ISD::FCEIL:
    return "ceil";
  case ISD::FTRUNC:
    return "trunc";
  case ISD::FRINT:
    return "rint";
  case ISD::FNEARBYINT:
    return "nearbyint";
  case ISD::FROUND:
    return "round";
  case ISD::FROUNDEVEN:
    return "roundeven";
  case ISD::FMINNUM:
    return "fmin";
  case ISD::FMAXNUM:
    return "fmax";
  case ISD::FMA:
    return "fma";
  default:
    return nullptr;
  }
}

// A call to a runtime function by name. The generic libcall tables do not know
// the __m48_ names or the C names for this type, so the call is built by hand.
// The call is pure, so it hangs off the entry node like the generic libcalls.
SDValue Mera400TargetLowering::makeFloatCall(SelectionDAG &DAG,
                                             const SDLoc &DL, const char *Name,
                                             EVT RetVT,
                                             ArrayRef<SDValue> Ops) const {
  LLVMContext &Ctx = *DAG.getContext();
  TargetLowering::ArgListTy Args;
  for (SDValue Op : Ops)
    Args.emplace_back(Op, Op.getValueType().getTypeForEVT(Ctx));
  Type *RetTy = RetVT.getTypeForEVT(Ctx);
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, RetTy, RetTy,
                    DAG.getExternalSymbol(Name,
                                          getPointerTy(DAG.getDataLayout())),
                    std::move(Args));
  return LowerCallTo(CLI).first;
}

// There are no NaNs in the native format, so ordered and unordered
// predicates are the same as the plain ones; "ordered" is always true and
// "unordered" always false. Returns the integer condition to apply to the
// result of __m48_cmp (-1, 0 or 1) compared with zero. For always true or
// false conditions Constant is set to 1 or 0 instead.
static ISD::CondCode getFloatCondition(ISD::CondCode CC, int &Constant) {
  Constant = -1;
  switch (CC) {
  case ISD::SETOEQ:
  case ISD::SETUEQ:
  case ISD::SETEQ:
    return ISD::SETEQ;
  case ISD::SETONE:
  case ISD::SETUNE:
  case ISD::SETNE:
    return ISD::SETNE;
  case ISD::SETOLT:
  case ISD::SETULT:
  case ISD::SETLT:
    return ISD::SETLT;
  case ISD::SETOLE:
  case ISD::SETULE:
  case ISD::SETLE:
    return ISD::SETLE;
  case ISD::SETOGT:
  case ISD::SETUGT:
  case ISD::SETGT:
    return ISD::SETGT;
  case ISD::SETOGE:
  case ISD::SETUGE:
  case ISD::SETGE:
    return ISD::SETGE;
  case ISD::SETO:
  case ISD::SETTRUE:
  case ISD::SETTRUE2:
    Constant = 1;
    return ISD::SETEQ;
  default:
    Constant = 0;
    return ISD::SETEQ;
  }
}

// Replaces a comparison of two floating point values with a comparison of
// the result of __m48_cmp with zero. The operands are returned in LHS and RHS
// and the condition in CC; an always true or false condition compares zero
// with zero.
void Mera400TargetLowering::lowerFloatCompare(SelectionDAG &DAG,
                                              const SDLoc &DL, SDValue &LHS,
                                              SDValue &RHS,
                                              ISD::CondCode &CC) const {
  int Constant;
  ISD::CondCode IntCC = getFloatCondition(CC, Constant);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i16);
  if (Constant >= 0) {
    LHS = RHS = Zero;
    CC = Constant ? ISD::SETEQ : ISD::SETNE;
    return;
  }
  LHS = makeFloatCall(DAG, DL, "__m48_cmp", MVT::i16, {LHS, RHS});
  RHS = Zero;
  CC = IntCC;
}

// Rewrites one floating point node into native format operations; returns an
// empty value when the node is not one this code handles.
SDValue Mera400TargetLowering::combineFloat(SDNode *N,
                                            SelectionDAG &DAG) const {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  unsigned Opc = N->getOpcode();
  auto Op = [&](unsigned I) { return nativeOperand(N->getOperand(I), DAG); };

  // Comparisons have an integer or boolean result; what matters is the type of
  // the compared operands.
  switch (Opc) {
  case ISD::SETCC:
    if (N->getOperand(0).getValueType() != MVT::f64)
      return SDValue();
    {
      SDValue LHS = Op(0), RHS = Op(1);
      ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
      lowerFloatCompare(DAG, DL, LHS, RHS, CC);
      return DAG.getSetCC(DL, VT, LHS, RHS, CC);
    }
  case ISD::BR_CC:
    if (N->getOperand(2).getValueType() != MVT::f64)
      return SDValue();
    {
      SDValue LHS = Op(2), RHS = Op(3);
      ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(1))->get();
      lowerFloatCompare(DAG, DL, LHS, RHS, CC);
      return DAG.getNode(ISD::BR_CC, DL, MVT::Other, N->getOperand(0),
                         DAG.getCondCode(CC), LHS, RHS, N->getOperand(4));
    }
  case ISD::SELECT_CC:
    if (N->getOperand(0).getValueType() != MVT::f64)
      return SDValue();
    {
      SDValue LHS = Op(0), RHS = Op(1);
      ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(4))->get();
      lowerFloatCompare(DAG, DL, LHS, RHS, CC);
      return DAG.getNode(ISD::SELECT_CC, DL, VT, LHS, RHS, Op(2), Op(3),
                         DAG.getCondCode(CC));
    }
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP: {
    if (VT != MVT::f64)
      return SDValue();
    bool Signed = Opc == ISD::SINT_TO_FP;
    SDValue Src = N->getOperand(0);
    unsigned Bits = Src.getValueSizeInBits();
    if (Bits > 64) {
      fail(DL, DAG, "Mera400: integers wider than 64 bits cannot be converted "
                    "to floating point");
      return DAG.getUNDEF(VT);
    }
    // Narrow integers are extended to 32 bits for the 32-bit helper.
    if (Bits < 32)
      Src = DAG.getNode(Signed ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, DL,
                        MVT::i32, Src);
    const char *Name = Bits <= 32 ? (Signed ? "__m48_fromi32" : "__m48_fromu32")
                                  : (Signed ? "__m48_fromi64" : "__m48_fromu64");
    return makeFloatCall(DAG, DL, Name, MVT::f64, {Src});
  }
  default:
    break;
  }

  if (Opc == ISD::FP_TO_SINT || Opc == ISD::FP_TO_UINT) {
    if (N->getOperand(0).getValueType() != MVT::f64)
      return SDValue();
    bool Signed = Opc == ISD::FP_TO_SINT;
    unsigned Bits = VT.getSizeInBits();
    if (Bits > 64) {
      fail(DL, DAG, "Mera400: floating point cannot be converted to integers "
                    "wider than 64 bits");
      return DAG.getUNDEF(VT);
    }
    // Narrow results come from the 32-bit helper, truncated.
    EVT HelperVT = Bits <= 32 ? MVT::i32 : MVT::i64;
    const char *Name = Bits <= 32 ? (Signed ? "__m48_toi32" : "__m48_tou32")
                                  : (Signed ? "__m48_toi64" : "__m48_tou64");
    SDValue R = makeFloatCall(DAG, DL, Name, HelperVT, {Op(0)});
    return VT == HelperVT ? R : DAG.getNode(ISD::TRUNCATE, DL, VT, R);
  }

  if (VT != MVT::f64)
    return SDValue();

  switch (Opc) {
  case ISD::FNEG:
    return makeFloatCall(DAG, DL, "__m48_neg", VT, {Op(0)});
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FDIV: {
    SDValue A = Op(0), B = Op(1);
    if (!DAG.getSubtarget<Mera400Subtarget>().hasAWP()) {
      const char *Name = Opc == ISD::FADD   ? "__m48_add"
                         : Opc == ISD::FSUB ? "__m48_sub"
                         : Opc == ISD::FMUL ? "__m48_mul"
                                            : "__m48_div";
      return makeFloatCall(DAG, DL, Name, VT, {A, B});
    }
    unsigned NodeOpc = Opc == ISD::FADD   ? Mera400ISD::FADD48
                       : Opc == ISD::FSUB ? Mera400ISD::FSUB48
                       : Opc == ISD::FMUL ? Mera400ISD::FMUL48
                                          : Mera400ISD::FDIV48;
    SmallVector<SDValue, 6> Ops;
    splitFloat(A, Ops, DL, DAG);
    splitFloat(B, Ops, DL, DAG);
    SDValue R = DAG.getNode(
        NodeOpc, DL, DAG.getVTList(MVT::i16, MVT::i16, MVT::i16), Ops);
    return joinFloat(R.getValue(0), R.getValue(1), R.getValue(2), DL, DAG);
  }
  case ISD::FABS:
  case ISD::FCOPYSIGN: {
    // Negate when the sign differs from the wanted one. The sign is the top
    // bit of the first word, so the exclusive or of the first words has it set
    // exactly when the signs differ; for the absolute value the wanted sign is
    // positive, so the first word itself is tested.
    SmallVector<SDValue, 3> X, Y;
    splitFloat(Op(0), X, DL, DAG);
    SDValue Test = X[0];
    if (Opc == ISD::FCOPYSIGN) {
      splitFloat(Op(1), Y, DL, DAG);
      Test = DAG.getNode(ISD::XOR, DL, MVT::i16, X[0], Y[0]);
    }
    SDValue R = DAG.getNode(Mera400ISD::FNEGIF, DL,
                            DAG.getVTList(MVT::i16, MVT::i16, MVT::i16), X[0],
                            X[1], X[2], Test);
    return joinFloat(R.getValue(0), R.getValue(1), R.getValue(2), DL, DAG);
  }
  default:
    break;
  }

  if (const char *Name = getMathFunction(Opc)) {
    SmallVector<SDValue, 3> Args;
    for (unsigned I = 0, E = N->getNumOperands(); I != E; ++I)
      Args.push_back(Op(I));
    return makeFloatCall(DAG, DL, Name, VT, Args);
  }
  return SDValue();
}

// A branch or select on a wide compare is turned into BR_CC/SELECT_CC before
// the types are legalized, which would otherwise split the compare into its
// limbs and lose the chain of branches.
SDValue Mera400TargetLowering::PerformDAGCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  if (!DCI.isBeforeLegalize())
    return SDValue();
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  // Constants stored or selected are not seen by any other rewrite, so they
  // are converted here.
  if (N->getOpcode() == ISD::STORE) {
    auto *St = cast<StoreSDNode>(N);
    SDValue Value = St->getValue();
    if (!isa<ConstantFPSDNode>(Value) || Value.getValueType() != MVT::f64)
      return SDValue();
    return DAG.getStore(St->getChain(), DL, nativeOperand(Value, DAG),
                        St->getBasePtr(), St->getMemOperand());
  }
  if (N->getOpcode() == ISD::SELECT && N->getValueType(0) == MVT::f64 &&
      (isa<ConstantFPSDNode>(N->getOperand(1)) ||
       isa<ConstantFPSDNode>(N->getOperand(2))))
    return DAG.getNode(ISD::SELECT, DL, MVT::f64, N->getOperand(0),
                       nativeOperand(N->getOperand(1), DAG),
                       nativeOperand(N->getOperand(2), DAG));
  if (N->getOpcode() != ISD::BRCOND && N->getOpcode() != ISD::SELECT)
    return combineFloat(N, DAG);
  if (N->getOpcode() == ISD::BRCOND) {
    SDValue Cond = N->getOperand(1);
    if (Cond.getOpcode() != ISD::SETCC || !Cond.hasOneUse() ||
        !isWideInt(Cond.getOperand(0).getValueType()))
      return SDValue();
    return DAG.getNode(ISD::BR_CC, DL, MVT::Other, N->getOperand(0),
                       Cond.getOperand(2), Cond.getOperand(0),
                       Cond.getOperand(1), N->getOperand(2));
  }
  if (N->getOpcode() == ISD::SELECT) {
    SDValue Cond = N->getOperand(0);
    if (Cond.getOpcode() != ISD::SETCC || !Cond.hasOneUse() ||
        !isWideInt(Cond.getOperand(0).getValueType()))
      return SDValue();
    return DAG.getNode(ISD::SELECT_CC, DL, N->getValueType(0),
                       Cond.getOperand(0), Cond.getOperand(1),
                       N->getOperand(1), N->getOperand(2), Cond.getOperand(2));
  }
  return SDValue();
}

bool Mera400TargetLowering::decomposeMulByConstant(LLVMContext &, EVT VT,
                                                   SDValue C) const {
  if (VT != MVT::i16 && VT != MVT::i32)
    return false;
  auto *CN = dyn_cast<ConstantSDNode>(C);
  if (!CN)
    return false;
  const APInt &V = CN->getAPIntValue();
  return V.isPowerOf2() || (V - 1).isPowerOf2() || (V + 1).isPowerOf2();
}

namespace {
// One compare-and-branch of a wide compare: compare limb Limb (0 is the
// highest) and go to the true or the false target if CC holds.
struct ChainStep {
  unsigned Limb;
  Mera400::CondCode CC;
  bool ToTrue;
};
} // end anonymous namespace

static Mera400::CondCode toUnsignedCC(Mera400::CondCode CC) {
  switch (CC) {
  case Mera400::CC_LT:
    return Mera400::CC_ULT;
  case Mera400::CC_LE:
    return Mera400::CC_ULE;
  case Mera400::CC_GT:
    return Mera400::CC_UGT;
  case Mera400::CC_GE:
    return Mera400::CC_UGE;
  default:
    return CC;
  }
}

// The limbs are compared from the highest: the first limbs that differ decide
// the order, the highest one with the signedness of the compare, the others
// unsigned (they are magnitude bits). The last limb uses the compare as is
// (unsigned) and falls through to the false target.
static void buildChain(Mera400::CondCode CC, unsigned N,
                       SmallVectorImpl<ChainStep> &Steps) {
  if (CC == Mera400::CC_EQ) {
    for (unsigned I = 0; I + 1 != N; ++I)
      Steps.push_back({I, Mera400::CC_NE, false});
    Steps.push_back({N - 1, Mera400::CC_EQ, true});
    return;
  }
  if (CC == Mera400::CC_NE) {
    for (unsigned I = 0; I != N; ++I)
      Steps.push_back({I, Mera400::CC_NE, true});
    return;
  }
  bool Less = CC == Mera400::CC_LT || CC == Mera400::CC_LE ||
              CC == Mera400::CC_ULT || CC == Mera400::CC_ULE;
  bool Signed = !Mera400::isUnsignedCC(CC);
  for (unsigned I = 0; I + 1 != N; ++I) {
    bool S = Signed && I == 0;
    Mera400::CondCode Strict =
        Less ? (S ? Mera400::CC_LT : Mera400::CC_ULT)
             : (S ? Mera400::CC_GT : Mera400::CC_UGT);
    Steps.push_back({I, Strict, true});
    Steps.push_back({I, Mera400::CC_NE, false});
  }
  Steps.push_back({N - 1, toUnsignedCC(CC), true});
}

static void addSuccessorOnce(MachineBasicBlock *From, MachineBasicBlock *To) {
  if (!From->isSuccessor(To))
    From->addSuccessor(To);
}

// Emits the chain of BR_CC for the compare of the limbs of MI that start at
// operand 0 (first value) and N (second value). Blocks[I] holds step I; step
// I jumps to the true or false target and otherwise falls into Blocks[I+1]
// (the last one falls into Next).
static void emitChain(MachineInstr &MI, unsigned N,
                      ArrayRef<ChainStep> Steps,
                      ArrayRef<MachineBasicBlock *> Blocks,
                      MachineBasicBlock *True, MachineBasicBlock *False,
                      MachineBasicBlock *Next, unsigned FirstOp,
                      const TargetInstrInfo &TII) {
  for (unsigned I = 0, E = Steps.size(); I != E; ++I) {
    MachineBasicBlock *P = Blocks[I];
    MachineBasicBlock *Target = Steps[I].ToTrue ? True : False;
    BuildMI(P, MI.getDebugLoc(), TII.get(Mera400::BR_CC))
        .addReg(MI.getOperand(FirstOp + Steps[I].Limb).getReg())
        .addReg(MI.getOperand(FirstOp + N + Steps[I].Limb).getReg())
        .addImm(Steps[I].CC)
        .addMBB(Target);
    addSuccessorOnce(P, Target);
    addSuccessorOnce(P, I + 1 != E ? Blocks[I + 1] : Next);
  }
}

// A wide compare-and-branch is a chain of blocks that each hold one BR_CC:
//   BB:  BR_CC step 0 ...   (blocks of the other steps follow)
//   cont: what came after the compare (usually a jump to the false target)
MachineBasicBlock *
Mera400TargetLowering::emitWideBranch(MachineInstr &MI,
                                      MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  MachineFunction *MF = BB->getParent();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  unsigned N = (MI.getNumOperands() - 2) / 2;
  auto CC = static_cast<Mera400::CondCode>(MI.getOperand(2 * N).getImm());
  MachineBasicBlock *True = MI.getOperand(2 * N + 1).getMBB();

  SmallVector<ChainStep, 8> Steps;
  buildChain(CC, N, Steps);

  SmallVector<MachineBasicBlock *, 8> Blocks = {BB};
  MachineFunction::iterator It = std::next(BB->getIterator());
  for (unsigned I = 1, E = Steps.size(); I != E; ++I) {
    Blocks.push_back(MF->CreateMachineBasicBlock(LLVMBB));
    MF->insert(It, Blocks.back());
  }
  MachineBasicBlock *Cont = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(It, Cont);

  Cont->splice(Cont->begin(), BB, std::next(MachineBasicBlock::iterator(MI)),
               BB->end());
  // The false target is still a successor of Cont; the true one is only
  // reached from the chain, unless both are the same block.
  bool SameTarget = BB->succ_size() == 1;
  Cont->transferSuccessorsAndUpdatePHIs(BB);

  emitChain(MI, N, Steps, Blocks, True, Cont, Cont, 0, TII);

  for (MachineInstr &Phi : True->phis()) {
    unsigned ContIdx = 0;
    for (unsigned I = 2, E = Phi.getNumOperands(); I < E; I += 2)
      if (Phi.getOperand(I).getMBB() == Cont)
        ContIdx = I;
    if (!ContIdx)
      continue;
    Register Val = Phi.getOperand(ContIdx - 1).getReg();
    for (unsigned I = 0, E = Steps.size(); I != E; ++I)
      if (Steps[I].ToTrue) {
        Phi.addOperand(*MF, MachineOperand::CreateReg(Val, false));
        Phi.addOperand(*MF, MachineOperand::CreateMBB(Blocks[I]));
      }
    if (!SameTarget) {
      Phi.removeOperand(ContIdx);
      Phi.removeOperand(ContIdx - 1);
    }
  }
  if (!SameTarget)
    Cont->removeSuccessor(True);
  MI.eraseFromParent();
  return Cont;
}

// A wide select is the same chain, with a PHI that takes the true value from
// the blocks that jump to Sink and the false value from Copy.
MachineBasicBlock *
Mera400TargetLowering::emitWideSelect(MachineInstr &MI,
                                      MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  MachineFunction *MF = BB->getParent();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  unsigned N = (MI.getNumOperands() - 4) / 2;
  auto CC = static_cast<Mera400::CondCode>(MI.getOperand(1 + 2 * N).getImm());
  Register TrueVal = MI.getOperand(2 + 2 * N).getReg();
  Register FalseVal = MI.getOperand(3 + 2 * N).getReg();

  SmallVector<ChainStep, 8> Steps;
  buildChain(CC, N, Steps);

  SmallVector<MachineBasicBlock *, 8> Blocks = {BB};
  MachineFunction::iterator It = std::next(BB->getIterator());
  for (unsigned I = 1, E = Steps.size(); I != E; ++I) {
    Blocks.push_back(MF->CreateMachineBasicBlock(LLVMBB));
    MF->insert(It, Blocks.back());
  }
  MachineBasicBlock *Copy = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *Sink = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(It, Copy);
  MF->insert(It, Sink);

  Sink->splice(Sink->begin(), BB, std::next(MachineBasicBlock::iterator(MI)),
               BB->end());
  Sink->transferSuccessorsAndUpdatePHIs(BB);
  Copy->addSuccessor(Sink);

  emitChain(MI, N, Steps, Blocks, Sink, Copy, Copy, 1, TII);

  MachineInstrBuilder Phi =
      BuildMI(*Sink, Sink->begin(), MI.getDebugLoc(), TII.get(Mera400::PHI),
              MI.getOperand(0).getReg());
  for (unsigned I = 0, E = Steps.size(); I != E; ++I)
    if (Steps[I].ToTrue)
      Phi.addReg(TrueVal).addMBB(Blocks[I]);
  Phi.addReg(FalseVal).addMBB(Copy);
  MI.eraseFromParent();
  return Sink;
}

// The four basic floating point operations on the AWP (arithmetic processor).
// Its instructions work on fixed registers: the first operand is in R1:R2:R3
// (the three words of a value, most significant first), the second one is in
// memory, and the result replaces the first operand in R1:R2:R3. So the second
// operand is stored to a three-word stack temporary first:
//   rw b0,t ; rw b1,t+1 ; rw b2,t+2 ; R1:R2:R3 = a ; af t ; d = R1:R2:R3
static MachineBasicBlock *emitAWPOperation(MachineInstr &MI,
                                           MachineBasicBlock *BB) {
  MachineFunction &MF = *BB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  unsigned Hardware;
  switch (MI.getOpcode()) {
  case Mera400::FADD48:
    Hardware = Mera400::AFrm;
    break;
  case Mera400::FSUB48:
    Hardware = Mera400::SFrm;
    break;
  case Mera400::FMUL48:
    Hardware = Mera400::MFrm;
    break;
  default:
    Hardware = Mera400::DFrm;
    break;
  }
  static const MCPhysReg Acc[] = {Mera400::R1, Mera400::R2, Mera400::R3};
  int FI = MF.getFrameInfo().CreateStackObject(6, Align(2), false);
  for (unsigned I = 0; I != 3; ++I) {
    MachineMemOperand *MMO = MF.getMachineMemOperand(
        MachinePointerInfo::getFixedStack(MF, FI, 2 * I),
        MachineMemOperand::MOStore, 2, Align(2));
    BuildMI(*BB, MI, DL, TII.get(Mera400::STWrm))
        .addReg(MI.getOperand(6 + I).getReg())
        .addFrameIndex(FI)
        .addImm(I)
        .addMemOperand(MMO);
  }
  for (unsigned I = 0; I != 3; ++I)
    BuildMI(*BB, MI, DL, TII.get(TargetOpcode::COPY), Acc[I])
        .addReg(MI.getOperand(3 + I).getReg());
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad, 6,
      Align(2));
  BuildMI(*BB, MI, DL, TII.get(Hardware))
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO);
  for (unsigned I = 0; I != 3; ++I)
    BuildMI(*BB, MI, DL, TII.get(TargetOpcode::COPY),
            MI.getOperand(I).getReg())
        .addReg(Acc[I]);
  MI.eraseFromParent();
  return BB;
}

// Negates a floating point value when a word is negative (fabs, copysign):
//   entry:  BR_CC w >= 0, sink
//   neg:    R1:R2:R3 = a ; call __m48_neg ; n = R1:R2:R3
//   sink:   d = PHI [a, entry], [n, neg]    (for each of the three words)
// The call is made by hand, so the function must be marked as making calls (it
// then saves R4, which holds its return address).
static MachineBasicBlock *emitNegateIf(MachineInstr &MI,
                                       MachineBasicBlock *BB) {
  MachineFunction &MF = *BB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  DebugLoc DL = MI.getDebugLoc();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator It = std::next(BB->getIterator());

  MachineBasicBlock *NegMBB = MF.CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = MF.CreateMachineBasicBlock(LLVMBB);
  MF.insert(It, NegMBB);
  MF.insert(It, SinkMBB);
  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);
  BB->addSuccessor(NegMBB);
  BB->addSuccessor(SinkMBB);
  NegMBB->addSuccessor(SinkMBB);

  BuildMI(BB, DL, TII.get(Mera400::BR_CCI))
      .addReg(MI.getOperand(6).getReg())
      .addImm(0)
      .addImm(Mera400::CC_GE)
      .addMBB(SinkMBB);

  static const MCPhysReg Acc[] = {Mera400::R1, Mera400::R2, Mera400::R3};
  for (unsigned I = 0; I != 3; ++I)
    BuildMI(NegMBB, DL, TII.get(TargetOpcode::COPY), Acc[I])
        .addReg(MI.getOperand(3 + I).getReg());
  MachineInstrBuilder Call =
      BuildMI(NegMBB, DL, TII.get(Mera400::CALL)).addExternalSymbol("__m48_neg");
  Call.addRegMask(TRI.getCallPreservedMask(MF, CallingConv::C));
  for (MCPhysReg R : Acc)
    Call.addReg(R, RegState::Implicit);
  for (MCPhysReg R : Acc)
    Call.addReg(R, RegState::ImplicitDefine);
  MF.getFrameInfo().setHasCalls(true);

  Register Negated[3];
  for (unsigned I = 0; I != 3; ++I) {
    Negated[I] = MRI.createVirtualRegister(&Mera400::GPR16RegClass);
    BuildMI(NegMBB, DL, TII.get(TargetOpcode::COPY), Negated[I]).addReg(Acc[I]);
  }
  for (unsigned I = 0; I != 3; ++I)
    BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(Mera400::PHI),
            MI.getOperand(I).getReg())
        .addReg(MI.getOperand(3 + I).getReg())
        .addMBB(BB)
        .addReg(Negated[I])
        .addMBB(NegMBB);
  MI.eraseFromParent();
  return SinkMBB;
}

// Pseudos marked usesCustomInserter need new basic blocks (a branch diamond or
// a chain), which instruction-selection patterns cannot create, so they are
// expanded here right after selection.
//
// select is a diamond: the compare-and-branch jumps over the block that
// copies the false value.
//   entry:  BR_CC cond, sink     (true: the PHI takes the true value)
//   copy:                        (false: falls through)
//   sink:   d = PHI [t, entry], [f, copy]
MachineBasicBlock *Mera400TargetLowering::EmitInstrWithCustomInserter(
    MachineInstr &MI, MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  case Mera400::BR_CC32:
  case Mera400::BR_CC64:
    return emitWideBranch(MI, BB);
  case Mera400::SELECT_CC32:
  case Mera400::SELECT_CC64:
    return emitWideSelect(MI, BB);
  case Mera400::FADD48:
  case Mera400::FSUB48:
  case Mera400::FMUL48:
  case Mera400::FDIV48:
    return emitAWPOperation(MI, BB);
  case Mera400::FNEGIF48:
    return emitNegateIf(MI, BB);
  default:
    break;
  }
  assert((MI.getOpcode() == Mera400::SELECT_CC ||
          MI.getOpcode() == Mera400::SELECT_CCI) &&
         "unexpected instruction with a custom inserter");
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = BB->getParent();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator It = std::next(BB->getIterator());

  MachineBasicBlock *CopyMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(It, CopyMBB);
  MF->insert(It, SinkMBB);

  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);
  BB->addSuccessor(CopyMBB);
  BB->addSuccessor(SinkMBB);
  CopyMBB->addSuccessor(SinkMBB);

  bool IsImm = MI.getOpcode() == Mera400::SELECT_CCI;
  BuildMI(BB, DL, TII.get(IsImm ? Mera400::BR_CCI : Mera400::BR_CC))
      .add(MI.getOperand(1))
      .add(MI.getOperand(2))
      .add(MI.getOperand(3))
      .addMBB(SinkMBB);
  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(Mera400::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(4).getReg())
      .addMBB(BB)
      .addReg(MI.getOperand(5).getReg())
      .addMBB(CopyMBB);
  MI.eraseFromParent();
  return SinkMBB;
}

SDValue Mera400TargetLowering::LowerOperation(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::ADDRSPACECAST:
    return LowerAddrSpaceCast(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    fail(DL, DAG, "Mera400: variable-sized stack allocation (alloca) is not "
                  "supported");
    return DAG.getMergeValues({DAG.getUNDEF(MVT::i16), Op.getOperand(0)}, DL);
  case ISD::SIGN_EXTEND_INREG:
    return LowerSignExtendInReg(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    return LowerShift(Op, DAG);
  case ISD::ROTL:
  case ISD::ROTR:
    return LowerRotate(Op, DAG);
  case ISD::LOAD:
    return LowerLoad(Op, DAG);
  case ISD::STORE:
    return LowerWideStore(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  default:
    llvm_unreachable("unexpected custom lowering");
  }
}

// Results travel in R1..R3 only; a result that needs more (i64) makes
// CanLowerReturn fail and the generic code returns it through a hidden pointer.
// A floating point result is three words, so it fits.
bool Mera400TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(dropFloatPadding(Outs), RetCC_Mera400);
}

// Return values are copied to R1..R3 and RET_GLUE (a jump through R4, which
// holds the return address) is glued to the copies.
SDValue
Mera400TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                   bool IsVarArg,
                                   const SmallVectorImpl<ISD::OutputArg> &Outs,
                                   const SmallVectorImpl<SDValue> &AllOutVals,
                                   const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<SDValue, 4> RetOps(1, Chain);

  if (hasUnsupportedArg(Outs)) {
    fail(DL, DAG,
         "Mera400: returning an integer wider than 64 bits is not supported");
    return DAG.getNode(Mera400ISD::RET_GLUE, DL, MVT::Other, RetOps);
  }

  SmallVector<ISD::OutputArg, 8> OutParts = dropFloatPadding(Outs);
  SmallVector<SDValue, 8> OutVals;
  for (unsigned I = 0, E = Outs.size(); I != E; ++I)
    if (!isFloatPadding(Outs, I))
      OutVals.push_back(AllOutVals[I]);

  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(OutParts, RetCC_Mera400);

  SDValue Glue;
  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "Can only return in registers!");
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[I], Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(Mera400ISD::RET_GLUE, DL, MVT::Other, RetOps);
}
