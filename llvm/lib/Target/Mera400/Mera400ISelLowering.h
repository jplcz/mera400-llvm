//===-- Mera400ISelLowering.h - MERA-400 DAG lowering ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the selection DAG (directed acyclic graph) lowering hooks used by
// instruction selection (ISel).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400ISELLOWERING_H
#define LLVM_LIB_TARGET_MERA400_MERA400ISELLOWERING_H

#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class Mera400Subtarget;

/// Describes how LLVM IR is lowered for the MERA-400: the only legal type is
/// i16 (R1..R3 carry arguments and results), everything wider is split into
/// 16-bit limbs, high word first, and what the CPU cannot do becomes a
/// library call or a diagnostic.
class Mera400TargetLowering : public TargetLowering {
public:
  Mera400TargetLowering(const TargetMachine &TM, const Mera400Subtarget &STI);

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  bool areJTsAllowed(const Function *Fn) const override;

  // Multiplication is a library call, so multiplying by 2^k and 2^k +- 1 is
  // done with shifts and an addition instead.
  bool decomposeMulByConstant(LLVMContext &Context, EVT VT,
                              SDValue C) const override;

  // Claiming that negation and absolute value of a floating point value are
  // free keeps the generic combiner from rewriting them as an exclusive or or
  // an and with the IEEE sign bit mask, which is the wrong bit in the native
  // format (its mantissa is two's complement). They are still lowered by
  // combineFloat; "free" only means that nothing is converted into them.
  bool isFAbsFree(EVT VT) const override { return VT == MVT::f64; }
  bool isFNegFree(EVT VT) const override { return VT == MVT::f64; }

  // The generic "shift through a stack temporary" strategy for wide variable
  // shifts addresses the temporary with byte offsets. Pointers here count
  // words, so the temporary's halves landed twice as far apart as intended and
  // overwrote the spill slots (and the return address). Splitting the shift
  // into halves (SHL_PARTS, expanded with selects) needs no memory at all.
  ShiftLegalizationStrategy
  preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                     unsigned ExpansionFactor) const override {
    return ShiftLegalizationStrategy::ExpandToParts;
  }

  // Pointers count words, so byte alignment says nothing about their low bits
  // (otherwise FI+1 would be folded into an "or").
  void computeKnownBitsForStackObjectPointer(KnownBits &Known,
                                             const MachineFunction &MF,
                                             Align Alignment) const override {}

  // Pointers count words but the generic memory combines compute in bytes: a
  // narrowed load or a merged store would be addressed at the wrong place (or
  // become a byte access through a word pointer).
  bool shouldReduceLoadWidth(SDNode *Load, ISD::LoadExtType ExtTy, EVT NewVT,
                             std::optional<unsigned> ByteOffset =
                                 std::nullopt) const override {
    return false;
  }
  // DAGCombiner replaces "word = (word & mask) | value" or "word op= constant"
  // by a store of just the changed byte (or half), at the same pointer plus a
  // byte offset. A word pointer (address space 0) counts words, so that byte
  // went to the wrong place or used the word address as a byte address with
  // "rb" (for instance the large model read-modify-write of a "bool" global,
  // or "g |= 0x100" on a 16-bit global). Byte pointers (address space 1) are
  // lowered separately, so narrowing is refused for word pointers.
  bool canNarrowAccessByByteOffset(unsigned AS) const override {
    return AS != 0;
  }
  bool canMergeStoresTo(unsigned AS, EVT MemVT,
                        const MachineFunction &MF) const override {
    return false;
  }

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

private:
  SDValue combineFloat(SDNode *N, SelectionDAG &DAG) const;
  SDValue makeFloatCall(SelectionDAG &DAG, const SDLoc &DL, const char *Name,
                        EVT RetVT, ArrayRef<SDValue> Ops) const;
  void lowerFloatCompare(SelectionDAG &DAG, const SDLoc &DL, SDValue &LHS,
                         SDValue &RHS, ISD::CondCode &CC) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerAddrSpaceCast(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShift(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRotate(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSignExtendInReg(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerWideStore(SDValue Op, SelectionDAG &DAG) const;
  MachineBasicBlock *emitWideBranch(MachineInstr &MI,
                                    MachineBasicBlock *BB) const;
  MachineBasicBlock *emitWideSelect(MachineInstr &MI,
                                    MachineBasicBlock *BB) const;
  SDValue LowerCallResult(SDValue Chain, SDValue Glue,
                          CallingConv::ID CallConv, bool IsVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &DL, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;
};
} // namespace llvm

#endif
