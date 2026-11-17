//===-- Mera400ISelDAGToDAG.cpp - MERA-400 instruction selection
//------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instruction selection (ISel): matches the legalized selection DAG (directed
// acyclic graph) against the patterns in Mera400InstrInfo.td, which are
// generated into Mera400GenDAGISel.inc. This file adds what patterns cannot
// express: frame index nodes, carry-chain merging, and the address forms
// (register + displacement, or absolute) of memory operands.
//
//===----------------------------------------------------------------------===//

#include "Mera400.h"
#include "Mera400SelectionDAGInfo.h"
#include "Mera400TargetMachine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

using namespace llvm;

#define DEBUG_TYPE "mera400-isel"
#define PASS_NAME "MERA-400 DAG->DAG Pattern Instruction Selection"

namespace {
// The legacy pass manager wrapper and the new pass manager entry point
// (Mera400ISelDAGToDAGPass below) share this one selector implementation.
class Mera400DAGToDAGISel : public SelectionDAGISel {
public:
  Mera400DAGToDAGISel() = delete;
  Mera400DAGToDAGISel(Mera400TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

private:
#include "Mera400GenDAGISel.inc"

  void Select(SDNode *N) override;

  bool trySelectWideAddSub(SDNode *N);
  bool SelectAddrRM(SDValue Addr, SDValue &Base, SDValue &Disp);
  bool SelectAddrAbs(SDValue Addr, SDValue &Disp);
};

class Mera400DAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  Mera400DAGToDAGISelLegacy(Mera400TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<Mera400DAGToDAGISel>(TM, OptLevel)) {}
};
} // end anonymous namespace

char Mera400DAGToDAGISelLegacy::ID;

INITIALIZE_PASS(Mera400DAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createMera400ISelDag(Mera400TargetMachine &TM,
                                         CodeGenOptLevel OptLevel) {
  return new Mera400DAGToDAGISelLegacy(TM, OptLevel);
}

Mera400ISelDAGToDAGPass::Mera400ISelDAGToDAGPass(Mera400TargetMachine &TM,
                                                 CodeGenOptLevel OptLevel)
    : SelectionDAGISelPass(
          std::make_unique<Mera400DAGToDAGISel>(TM, OptLevel)) {}

void Mera400DAGToDAGISel::Select(SDNode *Node) {
  if (Node->isMachineOpcode()) {
    Node->setNodeId(-1);
    return;
  }
  // The address of a stack object is R7 plus an offset that is only known once
  // the frame is laid out, so it becomes a LEA pseudo that is resolved later.
  if (Node->getOpcode() == ISD::FrameIndex) {
    SDLoc DL(Node);
    SDValue FI = CurDAG->getTargetFrameIndex(
        cast<FrameIndexSDNode>(Node)->getIndex(), MVT::i16);
    ReplaceNode(Node, CurDAG->getMachineNode(Mera400::LEA, DL, MVT::i16, FI,
                                             CurDAG->getTargetConstant(
                                                 0, DL, MVT::i16)));
    return;
  }
  if ((Node->getOpcode() == ISD::UADDO_CARRY ||
       Node->getOpcode() == ISD::USUBO_CARRY) &&
      trySelectWideAddSub(Node))
    return;
  SelectCode(Node);
}

// The high half of a 32-bit add or subtract whose carry comes from the low
// half and is used by nothing else does not need the carry as a value: the
// hardware carry flag is consumed by the next instruction, "aw lo; ac hi".
// The ADD32/SUB32 pseudos keep both instructions together so nothing can
// clobber the flag between them.
bool Mera400DAGToDAGISel::trySelectWideAddSub(SDNode *N) {
  bool IsAdd = N->getOpcode() == ISD::UADDO_CARRY;
  // The fast register allocator of unoptimized code cannot place the pair's
  // four operands and two tied results when a value is also used afterwards,
  // so the separate instructions (with the carry in a register) are used.
  if (OptLevel == CodeGenOptLevel::None)
    return false;
  SDValue Carry = N->getOperand(2);
  SDNode *Low = Carry.getNode();
  if (Carry.getResNo() != 1 ||
      Low->getOpcode() != (IsAdd ? ISD::UADDO : ISD::USUBO) ||
      !Carry.hasOneUse() || N->hasAnyUseOfValue(1))
    return false;
  SDLoc DL(N);
  SDValue Ops[] = {N->getOperand(0), Low->getOperand(0), N->getOperand(1),
                   Low->getOperand(1)};
  SDNode *Pair;
  if (IsAdd)
    Pair = CurDAG->getMachineNode(Mera400::ADD32, DL, MVT::i16, MVT::i16, Ops);
  else
    Pair = CurDAG->getMachineNode(Mera400::SUB32, DL, MVT::i16, MVT::i16,
                                  MVT::i16, Ops);
  ReplaceUses(SDValue(N, 0), SDValue(Pair, 0));
  ReplaceUses(SDValue(Low, 0), SDValue(Pair, 1));
  CurDAG->RemoveDeadNode(N);
  return true;
}

// Splits an address into an optional symbol or constant and a remainder.
// Constant displacements are in the units of the pointer, which the IR
// lowering pass made words for address space 0.
static bool getSymbolOrConst(SDValue N, SelectionDAG &DAG, SDValue &Out) {
  SDLoc DL(N);
  if (N.getOpcode() == Mera400ISD::WRAPPER) {
    Out = N.getOperand(0);
    return true;
  }
  if (auto *C = dyn_cast<ConstantSDNode>(N)) {
    Out = DAG.getSignedTargetConstant(C->getSExtValue(), DL, MVT::i16);
    return true;
  }
  return false;
}

// Selects the absolute address form: a symbol (possibly plus a constant) or a
// plain constant, which becomes the extra instruction word of "tw r, addr".
bool Mera400DAGToDAGISel::SelectAddrAbs(SDValue Addr, SDValue &Disp) {
  if (getSymbolOrConst(Addr, *CurDAG, Disp))
    return true;
  if (Addr.getOpcode() == ISD::ADD && isa<ConstantSDNode>(Addr.getOperand(1)) &&
      Addr.getOperand(0).getOpcode() == Mera400ISD::WRAPPER) {
    auto *GA = dyn_cast<GlobalAddressSDNode>(Addr.getOperand(0).getOperand(0));
    if (!GA)
      return false;
    int64_t Off = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    Disp = CurDAG->getTargetGlobalAddress(GA->getGlobal(), SDLoc(Addr),
                                          MVT::i16, GA->getOffset() + Off);
    return true;
  }
  return false;
}

// Selects base register + displacement ("tw r1, disp+r2"). A frame index base
// is kept symbolic and replaced by R7 after register allocation.
bool Mera400DAGToDAGISel::SelectAddrRM(SDValue Addr, SDValue &Base,
                                       SDValue &Disp) {
  SDLoc DL(Addr);
  auto Zero = [&] { return CurDAG->getTargetConstant(0, DL, MVT::i16); };
  auto MakeBase = [&](SDValue B) {
    if (auto *FI = dyn_cast<FrameIndexSDNode>(B))
      return CurDAG->getTargetFrameIndex(FI->getIndex(), MVT::i16);
    return B;
  };
  // Constants and symbols have no register part: use the absolute form.
  SDValue Dummy;
  if (getSymbolOrConst(Addr, *CurDAG, Dummy))
    return false;
  if (Addr.getOpcode() == ISD::ADD) {
    SDValue L = Addr.getOperand(0), R = Addr.getOperand(1);
    if (auto *C = dyn_cast<ConstantSDNode>(R)) {
      if (SelectAddrAbs(Addr, Dummy))
        return false;
      Base = MakeBase(L);
      Disp = CurDAG->getSignedTargetConstant(C->getSExtValue(), DL, MVT::i16);
      return true;
    }
    if (R.getOpcode() == Mera400ISD::WRAPPER) {
      Base = MakeBase(L);
      Disp = R.getOperand(0);
      return true;
    }
  }
  Base = MakeBase(Addr);
  Disp = Zero();
  return true;
}
