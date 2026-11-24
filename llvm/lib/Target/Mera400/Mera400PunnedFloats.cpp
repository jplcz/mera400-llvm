//===-- Mera400PunnedFloats.cpp - Keep type punned doubles in memory ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LLVM sees a MERA-400 "double" as an IEEE double, but in memory it is the
// native format (see Mera400FloatFormat.h). Storing four 16-bit words and
// loading a double from the same place (a C union, or a pointer cast) is
// therefore reading the native image, but the optimizer does not know that:
// SROA (scalar replacement of aggregates), GVN (global value numbering),
// InstCombine and others forward the stored words to the load as
// "bitcast i64 to double" and fold the constant as an IEEE bit pattern, which
// the back end then converts as an IEEE value (and gets 0 or garbage).
//
// The back end cannot repair that afterwards (a folded constant looks like any
// other floating point constant), so this pass runs at the very start of the
// optimization pipeline and makes every load and store of a memory object
// volatile when the object is accessed both as a double and as something else
// at overlapping addresses. A volatile access is never forwarded, folded or
// removed, so the words really go to memory and the load really reads them.
// Objects that are used only as doubles (the vast majority) and objects that
// are never used as doubles are not touched, so ordinary code is optimized as
// before.
//
// What it does not see: accesses through pointers that come from elsewhere (a
// function argument or the result of a call), and memcpy/memmove/memset
// between a double and an object of another type. Those must be written with
// volatile or a union in the same function.
//
//===----------------------------------------------------------------------===//

#include "Mera400.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"

using namespace llvm;

namespace {

struct Region {
  int64_t Begin;
  int64_t End; // exclusive; Unknown offsets cover everything
  bool IsDouble;
};

constexpr int64_t Everything = INT64_MAX;

bool overlap(const Region &A, const Region &B) {
  return A.Begin < B.End && B.Begin < A.End;
}

// The scalar leaves of a type that matter here: double, and any other scalar.
// An array of a non-double element type is one region.
void collectLeaves(const DataLayout &DL, Type *Ty, int64_t Offset,
                   SmallVectorImpl<Region> &Out) {
  if (Ty->isDoubleTy()) {
    Out.push_back({Offset, Offset + 8, true});
  } else if (auto *ST = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(ST);
    for (unsigned I = 0, E = ST->getNumElements(); I != E; ++I)
      collectLeaves(DL, ST->getElementType(I), Offset + SL->getElementOffset(I),
                    Out);
  } else if (auto *AT = dyn_cast<ArrayType>(Ty)) {
    Type *Elt = AT->getElementType();
    uint64_t Size = DL.getTypeAllocSize(Elt);
    if (!Elt->isDoubleTy() && !Elt->isAggregateType()) {
      Out.push_back({Offset, Offset + (int64_t)(Size * AT->getNumElements()),
                     false});
      return;
    }
    for (uint64_t I = 0, E = AT->getNumElements(); I != E && I < 64; ++I)
      collectLeaves(DL, Elt, Offset + I * Size, Out);
  } else if (Ty->isSized()) {
    Out.push_back({Offset, Offset + (int64_t)DL.getTypeStoreSize(Ty), false});
  }
}

struct Access {
  Instruction *I;
  Region R;
};

// Collects the loads and stores that reach Root through getelementptr,
// address space casts and bitcasts.
void collectAccesses(const DataLayout &DL, Value *Root,
                     SmallVectorImpl<Access> &Out) {
  struct Item {
    Value *V;
    int64_t Off; // Everything when not a constant
  };
  SmallVector<Item> Work = {{Root, 0}};
  SmallPtrSet<Value *, 16> Seen;
  while (!Work.empty()) {
    Item It = Work.pop_back_val();
    if (!Seen.insert(It.V).second)
      continue;
    for (User *U : It.V->users()) {
      auto Range = [&](Type *Ty) {
        int64_t Size = DL.getTypeStoreSize(Ty);
        if (It.Off == Everything)
          return Region{0, Everything, Ty->isDoubleTy()};
        return Region{It.Off, It.Off + Size, Ty->isDoubleTy()};
      };
      if (auto *L = dyn_cast<LoadInst>(U)) {
        if (!L->isAtomic() && L->getType()->isSized())
          Out.push_back({L, Range(L->getType())});
      } else if (auto *S = dyn_cast<StoreInst>(U)) {
        if (S->getPointerOperand() == It.V && !S->isAtomic() &&
            S->getValueOperand()->getType()->isSized())
          Out.push_back({S, Range(S->getValueOperand()->getType())});
      } else if (auto *GEP = dyn_cast<GEPOperator>(U)) {
        APInt Constant(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
        int64_t Off = Everything;
        if (It.Off != Everything && GEP->accumulateConstantOffset(DL, Constant))
          Off = It.Off + Constant.getSExtValue();
        Work.push_back({GEP, Off});
      } else if (isa<AddrSpaceCastInst>(U) || isa<AddrSpaceCastOperator>(U) ||
                 isa<BitCastInst>(U)) {
        Work.push_back({U, It.Off});
      }
    }
  }
}

// Makes the accesses of one memory object volatile if it is used as a double
// and as something else in the same bytes.
bool markIfPunned(const DataLayout &DL, Value *Root, Type *ObjectTy,
                  bool HasInitializerContent) {
  SmallVector<Access> Accesses;
  collectAccesses(DL, Root, Accesses);
  if (Accesses.empty())
    return false;
  SmallVector<Region> Doubles, Others;
  for (const Access &A : Accesses)
    (A.R.IsDouble ? Doubles : Others).push_back(A.R);
  SmallVector<Region> Leaves;
  collectLeaves(DL, ObjectTy, 0, Leaves);
  for (const Region &R : Leaves)
    if (R.IsDouble)
      Doubles.push_back(R);
    else if (HasInitializerContent)
      Others.push_back(R);
  bool Punned = false;
  for (const Region &D : Doubles)
    for (const Region &O : Others)
      Punned |= overlap(D, O);
  if (!Punned)
    return false;
  bool Changed = false;
  for (const Access &A : Accesses) {
    if (auto *L = dyn_cast<LoadInst>(A.I)) {
      Changed |= !L->isVolatile();
      L->setVolatile(true);
    } else {
      auto *S = cast<StoreInst>(A.I);
      Changed |= !S->isVolatile();
      S->setVolatile(true);
    }
  }
  return Changed;
}

} // end anonymous namespace

PreservedAnalyses Mera400PunnedFloatsPass::run(Module &M,
                                               ModuleAnalysisManager &) {
  const DataLayout &DL = M.getDataLayout();
  bool Changed = false;
  for (GlobalVariable &GV : M.globals()) {
    if (GV.getName().starts_with("llvm."))
      continue;
    // Zero is the same in both formats, other initial contents are words.
    bool Content = GV.hasInitializer() && !GV.getInitializer()->isNullValue() &&
                   !isa<UndefValue>(GV.getInitializer());
    Changed |= markIfPunned(DL, &GV, GV.getValueType(), Content);
  }
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (Instruction &I : F.getEntryBlock())
      if (auto *A = dyn_cast<AllocaInst>(&I))
        Changed |= markIfPunned(DL, A, A->getAllocatedType(), false);
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
