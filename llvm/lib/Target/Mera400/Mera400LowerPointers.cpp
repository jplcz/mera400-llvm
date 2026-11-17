//===-- Mera400LowerPointers.cpp - Pointer units and memory access checks -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A MERA-400 pointer in address space 0 is the address of a 16-bit word, but
// LLVM measures pointer arithmetic in 8-bit bytes. This pass rewrites every
// address space 0 "getelementptr" (GEP) into integer arithmetic in words, and
// rejects the memory accesses and intrinsics the backend cannot implement, so
// that nothing is silently miscompiled. Address space 1 pointers hold byte
// addresses, which match LLVM's units, and are left alone in the small model.
// Every C data pointer is such a byte pointer, so besides bytes, loads and
// stores of 16, 32 and 64-bit integers, pointers and doubles through them are
// rewritten here as word accesses at "byte address >> 1" (see wordPointerFor).
// The memory intrinsics (memcpy, memmove, memset) and the variable argument
// intrinsics (va_arg, va_copy, va_end) are also expanded here, into loads,
// stores and calls of the C library functions.
// In the large memory model a byte pointer is a 32-bit linear byte address
// and the hardware's byte instructions cannot reach all of memory, so byte
// accesses are rewritten as word accesses plus shifts and masks.
//
//===----------------------------------------------------------------------===//

#include "Mera400.h"
#include "MCTargetDesc/Mera400FloatFormat.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ReplaceConstant.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "mera400-lower-pointers"

namespace {

// Per-function implementation shared by the legacy and the new pass manager
// wrappers at the end of the file.
class LowerPointers {
  Function &F;
  bool Changed = false;
  bool Failed = false;

  // The data layout gives byte pointers (address space 1) 32 bits in the large
  // memory model and 16 bits in the small one.
  bool isLarge() const { return F.getDataLayout().getPointerSizeInBits(1) == 32; }

  void unsupported(const Instruction &I, const Twine &Msg) {
    Failed = true;
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "Mera400: " + Msg, I.getDebugLoc(), DS_Error));
  }

  // Code generation continues after an error has been reported, so leave
  // nothing behind that instruction selection would crash on.
  void discardBody();

  void expandConstantGEPs();
  void checkAccess(Instruction &I, Type *Ty, Value *Ptr, Align Alignment,
                   bool Atomic);
  void lowerGEP(GetElementPtrInst &GEP);
  void retargetToBytePointers();
  void splitAggregateAccesses();
  void widenBoolAccesses();
  Value *byteAddressOf(IRBuilderBase &B, Value *Ptr);

  // C data pointers are byte pointers (address space 1): word accesses
  // through them become accesses at the word address (byte address >> 1).
  Value *wordPointerFor(IRBuilderBase &B, Value *Ptr);
  Value *directWordPointer(IRBuilderBase &B, Value *Ptr);
  void lowerWordAccess(Instruction &I);
  static void eraseIfDead(Value *V);

  // Memory intrinsics and variable argument lists, replaced by loads, stores
  // and library calls before anything else looks at the function.
  void lowerMemIntrinsic(MemIntrinsic &MI);
  bool expandMemInline(MemIntrinsic &MI, uint64_t Size, unsigned MaxOps);
  void lowerVarArgs(Instruction &I);
  Value *asBytePointer(IRBuilderBase &B, Value *Ptr);

  // Floating point: only "double" is supported (it is the native format, see
  // Mera400FloatFormat.h); everything else is diagnosed.
  void checkFloatTypes(Instruction &I);
  void checkFloatIntrinsic(Instruction &I, const CallBase &Call);
  void materializeFloatConstants(Instruction &I);

  // Large memory model: a byte pointer is a 32-bit linear byte address and
  // the machine can only access whole words.
  void lowerByteAccess(Instruction &I);
  void lowerLargeAddrSpaceCast(AddrSpaceCastInst &ASC);

public:
  explicit LowerPointers(Function &F) : F(F) {}
  bool run();
};

} // end anonymous namespace

// Constant expressions are selected without going through this pass, so turn
// the address space 0 "getelementptr" ones into instructions first.
void LowerPointers::expandConstantGEPs() {
  SmallVector<Constant *> GEPs;
  SmallPtrSet<Constant *, 8> Seen;
  SmallVector<Constant *> Worklist;
  for (Instruction &I : instructions(F))
    for (Value *Op : I.operands())
      if (auto *C = dyn_cast<ConstantExpr>(Op))
        Worklist.push_back(C);
  while (!Worklist.empty()) {
    Constant *C = Worklist.pop_back_val();
    if (!Seen.insert(C).second)
      continue;
    if (auto *CE = dyn_cast<ConstantExpr>(C)) {
      if (CE->getOpcode() == Instruction::GetElementPtr &&
          CE->getType()->getPointerAddressSpace() == 0)
        GEPs.push_back(CE);
      else if (CE->getOpcode() == Instruction::AddrSpaceCast && isLarge())
        GEPs.push_back(CE);
      for (Value *Op : CE->operands())
        if (auto *OpC = dyn_cast<ConstantExpr>(Op))
          Worklist.push_back(OpC);
    }
  }
  if (!GEPs.empty())
    Changed |= convertUsersOfConstantsToInstructions(
        GEPs, &F, /*RemoveDeadConstants=*/false, /*IncludeSelf=*/true);
}

// Replaces the function by a single "unreachable" so that nothing the backend
// cannot handle reaches instruction selection after a diagnostic.
void LowerPointers::discardBody() {
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (!I.getType()->isVoidTy())
        I.replaceAllUsesWith(PoisonValue::get(I.getType()));
  BasicBlock &Entry = F.getEntryBlock();
  // Drop every instruction's references first: indirectbr and blockaddress
  // make blocks used by instructions of other blocks.
  for (BasicBlock &BB : F)
    BB.dropAllReferences();
  for (BasicBlock &BB : make_early_inc_range(F)) {
    if (&BB == &Entry)
      continue;
    BB.eraseFromParent();
  }
  while (!Entry.empty())
    Entry.back().eraseFromParent();
  new UnreachableInst(F.getContext(), &Entry);
}

// True for the types a word load or store moves: 16, 32 and 64-bit integers,
// pointers and doubles (the native floating point format, four words in memory
// of which three are significant).
static bool isWordAccessType(Type *Ty) {
  return Ty->isIntegerTy(16) || Ty->isIntegerTy(32) || Ty->isIntegerTy(64) ||
         Ty->isDoubleTy() || Ty->isPointerTy();
}

// Pointer plus a number of bytes. The index of a getelementptr must have the
// width the data layout gives the pointer's address space (16 bits for a word
// pointer and a small model byte pointer, 32 for a large model byte pointer),
// otherwise the IR is invalid.
static Value *offsetByBytes(IRBuilderBase &B, Value *Ptr, uint64_t Bytes) {
  if (!Bytes)
    return Ptr;
  Type *Index = B.GetInsertBlock()->getDataLayout().getIndexType(Ptr->getType());
  return B.CreateGEP(B.getInt8Ty(), Ptr, ConstantInt::get(Index, Bytes));
}

// Word pointers (address space 0) can only move 16, 32 and 64-bit integers,
// pointers and doubles, whole words at a time. Byte pointers (address space 1)
// can move bytes ("lb"/"rb") and, if 2-byte aligned, the same wide types
// (as a word access at byte address / 2). Everything else would need an access
// the hardware does not have, so it is diagnosed rather than miscompiled.
void LowerPointers::checkAccess(Instruction &I, Type *Ty, Value *Ptr,
                                Align Alignment, bool Atomic) {
  unsigned AS = Ptr->getType()->getPointerAddressSpace();
  if (Atomic)
    return unsupported(I, "atomic operations are not supported");
  if (AS > 1)
    return unsupported(I, "address space " + Twine(AS) + " is not supported");
  if (AS == 0) {
    if (!isWordAccessType(Ty))
      unsupported(I, "only 16, 32 and 64-bit integers, doubles and pointers "
                     "can be accessed through a word pointer (address space "
                     "0)");
    return;
  }
  if (Ty->isIntegerTy(8))
    return;
  if (!isWordAccessType(Ty))
    return unsupported(I, "only 8, 16, 32 and 64-bit integers, doubles and "
                          "pointers can be accessed through a byte pointer "
                          "(address space 1)");
  // A word access needs an even byte address; C guarantees it for objects of
  // 2-byte alignment, and the machine cannot do an unaligned access.
  if (Alignment < Align(2))
    unsupported(I, "misaligned access: a value wider than a byte accessed "
                   "through a byte pointer (address space 1) must be "
                   "2-byte aligned");
}

// Builds the address space 0 word pointer "Base + Constant bytes + the sum of
// the variable terms (index * scale bytes)", given that every offset is a whole
// number of words. Pointers are carried as 16-bit integers, which is what they
// are in hardware.
static Value *buildWordPointer(IRBuilderBase &B, Value *Base, Type *ResultTy,
                               const SmallMapVector<Value *, APInt, 4> &Variable,
                               const APInt &Constant) {
  Type *I16 = B.getInt16Ty();
  int64_t Words = Constant.getSExtValue() / 2;
  if (!Words && Variable.empty() && Base->getType() == ResultTy)
    return Base;
  Value *Sum = B.CreatePtrToInt(Base, I16);
  if (Words)
    Sum = B.CreateAdd(Sum, ConstantInt::getSigned(I16, Words));
  for (auto &[V, Scale] : Variable) {
    Value *Index = B.CreateSExtOrTrunc(V, I16);
    int64_t ScaleWords = Scale.getSExtValue() / 2;
    if (Scale[0]) {
      // A byte-scaled index (the vtable and member pointer code of the C++ ABI
      // adds byte offsets to word pointers); it is a multiple of two bytes
      // because everything it addresses is a word-aligned slot.
      Index = B.CreateAShr(Index, ConstantInt::getSigned(I16, 1));
      ScaleWords = 1;
    }
    if (ScaleWords != 1)
      Index = B.CreateMul(Index, ConstantInt::getSigned(I16, ScaleWords));
    Sum = B.CreateAdd(Sum, Index);
  }
  return B.CreateIntToPtr(Sum, ResultTy);
}

// Unoptimized Clang output loads and stores a whole structure or array value
// (for example a member function pointer). Those are replaced by one access
// per scalar, at the offsets the data layout gives them, before the byte
// offsets are turned into word offsets or byte pointers.
void LowerPointers::splitAggregateAccesses() {
  const DataLayout &DL = F.getDataLayout();
  SmallVector<Instruction *> Work;
  for (Instruction &I : instructions(F))
    if (auto *L = dyn_cast<LoadInst>(&I)) {
      if (L->getType()->isAggregateType() && L->isSimple())
        Work.push_back(&I);
    } else if (auto *S = dyn_cast<StoreInst>(&I)) {
      if (S->getValueOperand()->getType()->isAggregateType() && S->isSimple())
        Work.push_back(&I);
    }
  for (Instruction *I : Work) {
    IRBuilder<NoFolder> B(I);
    auto *L = dyn_cast<LoadInst>(I);
    Value *Ptr = L ? L->getPointerOperand()
                   : cast<StoreInst>(I)->getPointerOperand();
    Align Base = L ? L->getAlign() : cast<StoreInst>(I)->getAlign();
    // Visits the scalars of Ty in order; Offset is in bytes from Ptr.
    Value *Result = L ? PoisonValue::get(L->getType()) : nullptr;
    SmallVector<unsigned, 4> Path;
    auto Walk = [&](auto &&Self, Type *Ty, uint64_t Offset,
                    Value *Stored) -> void {
      if (auto *ST = dyn_cast<StructType>(Ty)) {
        const StructLayout *SL = DL.getStructLayout(ST);
        for (unsigned N = 0; N != ST->getNumElements(); ++N) {
          Path.push_back(N);
          Self(Self, ST->getElementType(N), Offset + SL->getElementOffset(N),
               Stored);
          Path.pop_back();
        }
      } else if (auto *AT = dyn_cast<ArrayType>(Ty)) {
        uint64_t Size = DL.getTypeAllocSize(AT->getElementType());
        for (unsigned N = 0; N != AT->getNumElements(); ++N) {
          Path.push_back(N);
          Self(Self, AT->getElementType(), Offset + N * Size, Stored);
          Path.pop_back();
        }
      } else {
        Value *Addr = offsetByBytes(B, Ptr, Offset);
        Align A = commonAlignment(Base, Offset);
        if (L) {
          Value *V = B.CreateAlignedLoad(Ty, Addr, A);
          Result = B.CreateInsertValue(Result, V, Path);
        } else {
          B.CreateAlignedStore(B.CreateExtractValue(Stored, Path), Addr, A);
        }
      }
    };
    Walk(Walk, L ? L->getType() : cast<StoreInst>(I)->getValueOperand()->getType(),
         0, L ? nullptr : cast<StoreInst>(I)->getValueOperand());
    if (L)
      L->replaceAllUsesWith(Result);
    I->eraseFromParent();
    Changed = true;
  }
}

// The byte pointer (address space 1) for a word pointer expression: the base
// is cast, and the offsets of the getelementptr chain on top of it are kept in
// bytes, where an odd offset is representable.
Value *LowerPointers::byteAddressOf(IRBuilderBase &B, Value *Ptr) {
  auto *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  if (!GEP || GEP->getPointerAddressSpace() != 0)
    return Ptr->getType()->getPointerAddressSpace() == 1
               ? Ptr
               : B.CreateAddrSpaceCast(Ptr, PointerType::get(F.getContext(), 1));
  const DataLayout &DL = F.getDataLayout();
  SmallMapVector<Value *, APInt, 4> Variable;
  APInt Constant(16, 0);
  if (!GEP->collectOffset(DL, 16, Variable, Constant))
    return nullptr;
  Value *Base = byteAddressOf(B, GEP->getPointerOperand());
  if (!Base)
    return nullptr;
  Type *Index = DL.getIndexType(Base->getType());
  Value *Result = offsetByBytes(B, Base, Constant.getSExtValue());
  for (auto &[V, Scale] : Variable) {
    Value *Offset = B.CreateSExtOrTrunc(V, Index);
    if (!Scale.isOne())
      Offset = B.CreateMul(Offset, ConstantInt::get(Index, Scale.getZExtValue()));
    Result = B.CreateGEP(B.getInt8Ty(), Result, Offset);
  }
  return Result;
}

// An i1 in memory (the optimizer makes them, for instance GlobalOpt shrinks a
// global that only ever holds two constants to an i1 global) is stored like a
// C "bool": in a byte holding 0 or 1. Loads and stores of i1 become byte
// (i8) accesses of the same address with a zero extension or truncation, and
// then follow the rules of every other byte access, including the rewrite of
// word pointers to byte pointers below. Only the low bit of a loaded byte is
// used, as the language reference leaves the other bits unspecified.
void LowerPointers::widenBoolAccesses() {
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    IRBuilder<NoFolder> B(&I);
    if (auto *L = dyn_cast<LoadInst>(&I)) {
      if (!L->getType()->isIntegerTy(1) || L->isAtomic())
        continue;
      auto *Byte = B.CreateAlignedLoad(B.getInt8Ty(), L->getPointerOperand(),
                                       L->getAlign(), L->isVolatile());
      Value *Bit = B.CreateTrunc(Byte, L->getType());
      Byte->takeName(L);
      L->replaceAllUsesWith(Bit);
      L->eraseFromParent();
      Changed = true;
    } else if (auto *S = dyn_cast<StoreInst>(&I)) {
      Value *V = S->getValueOperand();
      if (!V->getType()->isIntegerTy(1) || S->isAtomic())
        continue;
      B.CreateAlignedStore(B.CreateZExt(V, B.getInt8Ty()),
                           S->getPointerOperand(), S->getAlign(),
                           S->isVolatile());
      S->eraseFromParent();
      Changed = true;
    }
  }
}

// A word pointer can only address whole words, but the compiler front end
// (Clang) also reaches bytes of a stack slot or global through the plain
// address space 0 pointer it made (for instance the fields of a compound
// literal while it is built). A byte access, or an address that is an odd
// number of bytes into the object, is rewritten to go through a byte pointer.
void LowerPointers::retargetToBytePointers() {
  const DataLayout &DL = F.getDataLayout();
  auto IsOdd = [&](Value *Ptr) {
    auto *GEP = dyn_cast<GetElementPtrInst>(Ptr);
    if (!GEP || GEP->getPointerAddressSpace() != 0)
      return false;
    SmallMapVector<Value *, APInt, 4> Variable;
    APInt Constant(16, 0);
    return GEP->collectOffset(DL, 16, Variable, Constant) && Constant[0];
  };
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    unsigned OpNo;
    if (auto *L = dyn_cast<LoadInst>(&I)) {
      OpNo = L->getPointerOperandIndex();
      if (!L->getType()->isIntegerTy(8) && !IsOdd(L->getPointerOperand()))
        continue;
    } else if (auto *S = dyn_cast<StoreInst>(&I)) {
      OpNo = S->getPointerOperandIndex();
      if (!S->getValueOperand()->getType()->isIntegerTy(8) &&
          !IsOdd(S->getPointerOperand()))
        continue;
    } else if (auto *MI = dyn_cast<MemIntrinsic>(&I)) {
      // Dest is operand 0 and the source of a transfer operand 1.
      for (unsigned Op = 0; Op != (isa<MemTransferInst>(MI) ? 2u : 1u); ++Op) {
        Value *Ptr = MI->getArgOperand(Op);
        if (!IsOdd(Ptr))
          continue;
        IRBuilder<NoFolder> B(MI);
        if (Value *Bytes = byteAddressOf(B, Ptr)) {
          MI->setArgOperand(Op, Bytes);
          eraseIfDead(Ptr);
          Changed = true;
        }
      }
      continue;
    } else {
      continue;
    }
    Value *Ptr = I.getOperand(OpNo);
    if (Ptr->getType()->getPointerAddressSpace() != 0)
      continue;
    IRBuilder<NoFolder> B(&I);
    if (Value *Bytes = byteAddressOf(B, Ptr)) {
      I.setOperand(OpNo, Bytes);
      eraseIfDead(Ptr);
      Changed = true;
    }
  }
}

// A GEP computes "pointer + byte offset"; for a word pointer the machine needs
// "pointer + word offset". The offset is collected in bytes (constant part and
// scaled variable indices), required to be a whole number of words, and halved.
void LowerPointers::lowerGEP(GetElementPtrInst &GEP) {
  const DataLayout &DL = F.getDataLayout();
  if (GEP.getType()->isVectorTy())
    return unsupported(GEP, "vectors of pointers are not supported");
  unsigned AS = GEP.getPointerAddressSpace();
  if (AS == 1)
    return;
  if (AS != 0)
    return unsupported(GEP, "address space " + Twine(AS) + " is not supported");

  SmallMapVector<Value *, APInt, 4> Variable;
  APInt Constant(16, 0);
  if (!GEP.collectOffset(DL, 16, Variable, Constant))
    return unsupported(GEP, "cannot compute the offset of this "
                            "getelementptr");
  if (Constant[0])
    return unsupported(GEP, "a word pointer can only be offset by a whole "
                            "number of 16-bit words");
  for (auto &[V, Scale] : Variable)
    if (Scale[0] && !Scale.isOne())
      return unsupported(GEP, "a word pointer can only be indexed in whole "
                              "16-bit words or bytes");

  IRBuilder<> B(&GEP);
  Value *Result = buildWordPointer(B, GEP.getPointerOperand(), GEP.getType(),
                                   Variable, Constant);
  GEP.replaceAllUsesWith(Result);
  GEP.eraseFromParent();
  Changed = true;
}

void LowerPointers::eraseIfDead(Value *V) {
  while (auto *I = dyn_cast<Instruction>(V)) {
    if (!I->use_empty() || !(isa<GetElementPtrInst>(I) || isa<CastInst>(I)))
      return;
    V = I->getOperand(0);
    I->eraseFromParent();
  }
}

// The address space 0 word pointer for a byte pointer, computed at run time:
// the byte address shifted right by one (the octet selector bit is dropped).
// The small model lets the DAG do it as an address space cast; a large byte
// pointer is 32 bits wide and is shifted as an integer.
Value *LowerPointers::wordPointerFor(IRBuilderBase &B, Value *Ptr) {
  LLVMContext &Ctx = F.getContext();
  if (!isLarge())
    return B.CreateAddrSpaceCast(Ptr, PointerType::get(Ctx, 0));
  Type *I16 = B.getInt16Ty(), *I32 = B.getInt32Ty();
  Value *Addr = B.CreatePtrToInt(Ptr, I32);
  Value *Word = B.CreateTrunc(B.CreateLShr(Addr, ConstantInt::get(I32, 1)), I16);
  return B.CreateIntToPtr(Word, PointerType::get(Ctx, 0));
}

// Looks through "gep (addrspacecast X to byte pointer), offset" for the case
// the compiler itself creates: a stack slot or global (address space 0, a word
// address X) that was turned into a C pointer. If the whole offset is a
// multiple of two bytes, the word address is simply X plus half the offset and
// no shift happens at run time; this is what makes "int x; int *p = &x; *p"
// as cheap as "x". (LLVM's generic InferAddressSpaces pass does this too, but
// it also rewrites byte accesses and odd offsets into word pointers, which
// cannot express them, so it is not used.) Returns null if the pattern does not
// apply.
Value *LowerPointers::directWordPointer(IRBuilderBase &B, Value *Ptr) {
  const DataLayout &DL = F.getDataLayout();
  SmallMapVector<Value *, APInt, 4> Variable;
  APInt Constant(DL.getIndexSizeInBits(1), 0);
  Value *Cur = Ptr;
  while (auto *GEP = dyn_cast<GEPOperator>(Cur)) {
    if (GEP->getType()->isVectorTy() ||
        !GEP->collectOffset(DL, Constant.getBitWidth(), Variable, Constant))
      return nullptr;
    Cur = GEP->getPointerOperand();
  }
  auto *ASC = dyn_cast<AddrSpaceCastOperator>(Cur);
  if (!ASC || ASC->getSrcAddressSpace() != 0 || ASC->getDestAddressSpace() != 1)
    return nullptr;
  if (Constant[0])
    return nullptr;
  for (auto &[V, Scale] : Variable)
    if (Scale[0])
      return nullptr;
  Value *Base = ASC->getPointerOperand();
  return buildWordPointer(B, Base, Base->getType(), Variable, Constant);
}

// A load or store of a multi-byte type through a byte pointer.
void LowerPointers::lowerWordAccess(Instruction &I) {
  bool IsLoad = isa<LoadInst>(I);
  Value *Ptr = I.getOperand(IsLoad ? 0 : 1);
  IRBuilder<NoFolder> B(&I);
  Value *Word = directWordPointer(B, Ptr);
  if (!Word)
    Word = wordPointerFor(B, Ptr);
  I.setOperand(IsLoad ? 0 : 1, Word);
  eraseIfDead(Ptr);
  Changed = true;
}

// A byte pointer for Ptr (as seen by the C library): address space 1 is kept,
// a word pointer is widened with a left shift (the cast of slice 2).
Value *LowerPointers::asBytePointer(IRBuilderBase &B, Value *Ptr) {
  Type *BytePtr = PointerType::get(F.getContext(), 1);
  if (Ptr->getType()->getPointerAddressSpace() == 1)
    return Ptr;
  return B.CreateAddrSpaceCast(Ptr, BytePtr);
}

// Large model byte access: the CPU's byte instructions only reach a 16-bit
// byte address, so a 32-bit byte address is split into the word address
// (address >> 1) and the octet selector (bit 0). The word is read, and the
// byte is picked from it, or merged into it for a store (a read-modify-write
// of the whole word).
void LowerPointers::lowerByteAccess(Instruction &I) {
  auto *L = dyn_cast<LoadInst>(&I);
  auto *S = dyn_cast<StoreInst>(&I);
  Value *Ptr = L ? L->getPointerOperand() : S->getPointerOperand();
  bool Volatile = L ? L->isVolatile() : S->isVolatile();
  IRBuilder<NoFolder> B(&I);
  LLVMContext &Ctx = F.getContext();
  Type *I16 = B.getInt16Ty(), *I32 = B.getInt32Ty();
  Value *Addr = B.CreatePtrToInt(Ptr, I32);
  Value *WordAddr = B.CreateTrunc(B.CreateLShr(Addr, ConstantInt::get(I32, 1)), I16);
  Value *WordPtr = B.CreateIntToPtr(WordAddr, PointerType::get(Ctx, 0));
  // The even byte of a word is its high octet.
  Value *Even = B.CreateICmpEQ(
      B.CreateAnd(B.CreateTrunc(Addr, I16), ConstantInt::get(I16, 1)),
      ConstantInt::get(I16, 0));
  LoadInst *Word = B.CreateAlignedLoad(I16, WordPtr, Align(2), Volatile);
  if (L) {
    Value *Hi = B.CreateLShr(Word, ConstantInt::get(I16, 8));
    Value *Lo = B.CreateAnd(Word, ConstantInt::get(I16, 0xff));
    Value *Byte = B.CreateTrunc(B.CreateSelect(Even, Hi, Lo), B.getInt8Ty());
    L->replaceAllUsesWith(Byte);
  } else {
    Value *V = B.CreateZExt(S->getValueOperand(), I16);
    Value *InHi = B.CreateOr(B.CreateAnd(Word, ConstantInt::get(I16, 0x00ff)),
                             B.CreateShl(V, ConstantInt::get(I16, 8)));
    Value *InLo = B.CreateOr(B.CreateAnd(Word, ConstantInt::get(I16, 0xff00)), V);
    B.CreateAlignedStore(B.CreateSelect(Even, InHi, InLo), WordPtr, Align(2),
                         Volatile);
  }
  I.eraseFromParent();
  Changed = true;
}

// Large model casts between a 16-bit word pointer and a 32-bit byte pointer.
void LowerPointers::lowerLargeAddrSpaceCast(AddrSpaceCastInst &ASC) {
  IRBuilder<NoFolder> B(&ASC);
  Type *I16 = B.getInt16Ty(), *I32 = B.getInt32Ty();
  Value *Result;
  if (ASC.getSrcAddressSpace() == 0) {
    // Word address to byte address.
    Value *W = B.CreateZExt(B.CreatePtrToInt(ASC.getPointerOperand(), I16), I32);
    Result = B.CreateIntToPtr(B.CreateShl(W, ConstantInt::get(I32, 1)),
                              ASC.getType());
  } else {
    // Byte address to the word containing it.
    Value *A = B.CreatePtrToInt(ASC.getPointerOperand(), I32);
    Result = B.CreateIntToPtr(
        B.CreateTrunc(B.CreateLShr(A, ConstantInt::get(I32, 1)), I16),
        ASC.getType());
  }
  ASC.replaceAllUsesWith(Result);
  ASC.eraseFromParent();
  Changed = true;
}

// A copy or fill of a known small size is a few loads and stores. They are
// made here, in IR, rather than by the generic memcpy expansion of the
// instruction selector, because that expansion adds byte offsets to the
// pointer and an address space 0 pointer counts words. Whole words are moved
// when both sides are known to be 2-byte aligned (a word pointer always is),
// bytes otherwise. A memmove reads everything before it writes anything, so
// overlapping ranges work. Returns false if the expansion would be longer than
// allowed.
bool LowerPointers::expandMemInline(MemIntrinsic &MI, uint64_t Size,
                                    unsigned MaxOps) {
  auto AlignOf = [](Value *Ptr, MaybeAlign A) {
    return Ptr->getType()->getPointerAddressSpace() == 0 ? Align(2)
                                                         : A.valueOrOne();
  };
  auto *Transfer = dyn_cast<MemTransferInst>(&MI);
  Align DA = AlignOf(MI.getRawDest(), MI.getDestAlign());
  Align SA = Transfer ? AlignOf(Transfer->getRawSource(),
                                Transfer->getSourceAlign())
                      : Align(2);
  bool Words = DA >= Align(2) && SA >= Align(2);
  uint64_t Ops = Words ? Size / 2 + Size % 2 : Size;
  if (Ops > MaxOps)
    return false;

  IRBuilder<NoFolder> B(&MI);
  Type *I8 = B.getInt8Ty(), *I16 = B.getInt16Ty();
  bool Volatile = MI.isVolatile();
  Value *Dst = asBytePointer(B, MI.getRawDest());
  Value *Src = Transfer ? asBytePointer(B, Transfer->getRawSource()) : nullptr;
  auto At = [&](Value *Base, uint64_t Off) -> Value * {
    return offsetByBytes(B, Base, Off);
  };

  struct Chunk {
    uint64_t Offset;
    Type *Ty;
  };
  SmallVector<Chunk, 8> Chunks;
  uint64_t Unit = Words ? 2 : 1;
  uint64_t Off = 0;
  for (; Off + Unit <= Size; Off += Unit)
    Chunks.push_back({Off, Words ? I16 : I8});
  if (Off != Size)
    Chunks.push_back({Off, I8});
  auto ChunkAlign = [](const Chunk &C) {
    return C.Ty->isIntegerTy(16) ? Align(2) : Align(1);
  };

  if (!Transfer) {
    Value *Byte = cast<MemSetInst>(MI).getValue();
    Value *Pattern = nullptr;
    if (Words) {
      if (auto *C = dyn_cast<ConstantInt>(Byte)) {
        uint64_t V = C->getZExtValue() & 0xff;
        Pattern = ConstantInt::get(I16, V * 0x0101);
      } else {
        Value *Z = B.CreateZExt(Byte, I16);
        Pattern = B.CreateOr(B.CreateShl(Z, ConstantInt::get(I16, 8)), Z);
      }
    }
    for (const Chunk &C : Chunks)
      B.CreateAlignedStore(C.Ty == I16 ? Pattern : Byte, At(Dst, C.Offset),
                           ChunkAlign(C), Volatile);
  } else if (MI.getIntrinsicID() == Intrinsic::memmove) {
    SmallVector<Value *, 8> Loaded;
    for (const Chunk &C : Chunks)
      Loaded.push_back(
          B.CreateAlignedLoad(C.Ty, At(Src, C.Offset), ChunkAlign(C), Volatile));
    for (unsigned I = 0, E = Chunks.size(); I != E; ++I)
      B.CreateAlignedStore(Loaded[I], At(Dst, Chunks[I].Offset),
                           ChunkAlign(Chunks[I]), Volatile);
  } else {
    for (const Chunk &C : Chunks) {
      Value *V =
          B.CreateAlignedLoad(C.Ty, At(Src, C.Offset), ChunkAlign(C), Volatile);
      B.CreateAlignedStore(V, At(Dst, C.Offset), ChunkAlign(C), Volatile);
    }
  }
  return true;
}

// llvm.memcpy, llvm.memmove and llvm.memset (what struct copies and array
// initialisers become): inline for a small known size, else a call of the C
// library function (the same names the libc project provides, with C's types:
// byte pointers, and a size_t that is 16 bits in the small model and 32 in the
// large one). The call is marked "nobuiltin" so that the instruction selector
// does not turn it back into the generic expansion.
void LowerPointers::lowerMemIntrinsic(MemIntrinsic &MI) {
  const char *Name = nullptr;
  switch (MI.getIntrinsicID()) {
  case Intrinsic::memcpy:
  case Intrinsic::memcpy_inline:
    Name = "memcpy";
    break;
  case Intrinsic::memmove:
    Name = "memmove";
    break;
  case Intrinsic::memset:
  case Intrinsic::memset_inline:
    Name = "memset";
    break;
  default:
    return unsupported(MI, "this memory fill operation is not supported");
  }

  // An "inline" intrinsic must not become a call, so it is expanded however
  // long the sequence gets (up to a sanity limit).
  bool Forced = MI.isForceInlined();
  if (auto *Len = dyn_cast<ConstantInt>(MI.getLength())) {
    uint64_t Size = Len->getZExtValue();
    constexpr unsigned MaxInlineOps = 8, MaxForcedOps = 512;
    if (Size == 0 ||
        expandMemInline(MI, Size, Forced ? MaxForcedOps : MaxInlineOps)) {
      MI.eraseFromParent();
      Changed = true;
      return;
    }
  }
  if (Forced)
    return unsupported(MI, "an inline memory copy or fill must have a known "
                           "size of at most 512 operations");

  IRBuilder<NoFolder> B(&MI);
  LLVMContext &Ctx = F.getContext();
  Type *BytePtr = PointerType::get(Ctx, 1);
  Type *SizeTy = isLarge() ? B.getInt32Ty() : B.getInt16Ty();
  Value *Dst = asBytePointer(B, MI.getRawDest());
  Value *Len = B.CreateZExtOrTrunc(MI.getLength(), SizeTy);
  SmallVector<Value *, 3> Args = {Dst};
  SmallVector<Type *, 3> ArgTys = {BytePtr};
  if (auto *Transfer = dyn_cast<MemTransferInst>(&MI)) {
    Args.push_back(asBytePointer(B, Transfer->getRawSource()));
    ArgTys.push_back(BytePtr);
  } else {
    // C's memset takes the fill value as an int.
    Args.push_back(B.CreateZExt(cast<MemSetInst>(MI).getValue(),
                                B.getInt16Ty()));
    ArgTys.push_back(B.getInt16Ty());
  }
  Args.push_back(Len);
  ArgTys.push_back(SizeTy);
  // The large model's data layout puts functions in address space 1, but a
  // function this pass introduces has to be a real (address space 0) one.
  Module *M = F.getParent();
  FunctionType *LibTy = FunctionType::get(BytePtr, ArgTys, false);
  Function *Lib = M->getFunction(Name);
  if (!Lib)
    Lib = Function::Create(LibTy, GlobalValue::ExternalLinkage, 0, Name, M);
  FunctionCallee Callee(LibTy, Lib);
  CallInst *Call = B.CreateCall(Callee, Args);
  Call->addFnAttr(Attribute::NoBuiltin);
  MI.eraseFromParent();
  Changed = true;
}

// Variable argument lists. A va_list is a byte pointer to the next argument
// slot (see Mera400ISelLowering.cpp for how the caller lays the slots out), so
// everything except va_start is plain pointer arithmetic and is done here:
//   va_arg:  p = *ap; value = *(T *)p; *ap = p + size of T in bytes
//   va_copy: *dest = *src
//   va_end:  nothing
// A slot is one word, so integers take their size rounded up to whole words,
// and a double takes three words (the fourth, padding, word of its memory
// format is never passed; reading it with the double is harmless because the
// padding is ignored). va_start stays an intrinsic and is lowered by the
// instruction selector, which knows where the stack arguments are.
void LowerPointers::lowerVarArgs(Instruction &I) {
  LLVMContext &Ctx = F.getContext();
  IRBuilder<NoFolder> B(&I);
  Type *VaList = PointerType::get(Ctx, 1);
  if (isa<VAEndInst>(I)) {
    I.eraseFromParent();
    Changed = true;
    return;
  }
  if (auto *Start = dyn_cast<VAStartInst>(&I)) {
    // The instruction selector expects the va_list object at a word address.
    Value *Ap = Start->getArgList();
    if (Ap->getType()->getPointerAddressSpace() == 1) {
      Value *Word = B.CreateAddrSpaceCast(Ap, PointerType::get(Ctx, 0));
      Start->setCalledFunction(Intrinsic::getOrInsertDeclaration(
          F.getParent(), Intrinsic::vastart, {Word->getType()}));
      Start->setArgOperand(0, Word);
      Changed = true;
    }
    return;
  }
  if (auto *Copy = dyn_cast<VACopyInst>(&I)) {
    Value *V = B.CreateAlignedLoad(VaList, Copy->getSrc(), Align(2));
    B.CreateAlignedStore(V, Copy->getDest(), Align(2));
    I.eraseFromParent();
    Changed = true;
    return;
  }
  auto *VA = cast<VAArgInst>(&I);
  Type *Ty = VA->getType();
  if (!Ty->isIntegerTy() && !Ty->isFloatingPointTy() && !Ty->isPointerTy())
    return unsupported(I, "va_arg of an aggregate or vector is not supported "
                          "(the front end passes those indirectly)");
  const DataLayout &DL = F.getDataLayout();
  Value *Ap = VA->getPointerOperand();
  Value *Cur = B.CreateAlignedLoad(VaList, Ap, Align(2));
  // A value narrower than a word still occupies a whole slot; the C front end
  // never asks for one (default argument promotion), but hand-written IR may.
  Type *SlotTy = Ty->isIntegerTy() && Ty->getIntegerBitWidth() < 16
                     ? B.getInt16Ty()
                     : Ty;
  Value *Val;
  if (Ty->isDoubleTy()) {
    // Only three words of the four-word memory format are passed (the padding
    // word is dropped), so the fourth word must not be read: it belongs to
    // the next argument or lies beyond the stack. The value is assembled from
    // the three words, with a zero padding word, high word first.
    Value *Bits = B.getInt64(0);
    for (unsigned W = 0; W != 3; ++W) {
      Value *Word = B.CreateAlignedLoad(
          B.getInt16Ty(), offsetByBytes(B, Cur, 2 * W),
          Align(2));
      Word = B.CreateShl(B.CreateZExt(Word, B.getInt64Ty()), 48 - 16 * W);
      Bits = B.CreateOr(Bits, Word);
    }
    Val = B.CreateBitCast(Bits, Ty);
  } else {
    Val = B.CreateAlignedLoad(SlotTy, Cur, Align(2));
    if (SlotTy != Ty)
      Val = B.CreateTrunc(Val, Ty);
  }
  uint64_t Bytes = Ty->isDoubleTy()
                       ? 6
                       : alignTo(DL.getTypeStoreSize(SlotTy).getFixedValue(), 2);
  Value *Next = offsetByBytes(B, Cur, Bytes);
  B.CreateAlignedStore(Next, Ap, Align(2));
  VA->replaceAllUsesWith(Val);
  VA->eraseFromParent();
  Changed = true;
}

// The name of a floating point type the back end cannot handle, or null. The
// native format is one 48-bit format, so every other IEEE type (and vectors of
// floating point, which would need a vector unit) is rejected. A "float" in C
// is expected to be written as double in the IR.
static const char *unsupportedFloatType(Type *Ty) {
  if (Ty->isVectorTy() && Ty->getScalarType()->isFloatingPointTy())
    return "vectors of floating point values";
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    for (Type *Elt : ST->elements())
      if (const char *Name = unsupportedFloatType(Elt))
        return Name;
    return nullptr;
  }
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return unsupportedFloatType(AT->getElementType());
  if (Ty->isFloatingPointTy() && !Ty->isDoubleTy())
    return Ty->isFloatTy() ? "the float type (use double: the MERA-400 has "
                             "one floating point format)"
                           : "this floating point type (only double is "
                             "supported)";
  return nullptr;
}

const char *llvm::getUnsupportedFloatType(Type *Ty) {
  return unsupportedFloatType(Ty);
}

void LowerPointers::checkFloatTypes(Instruction &I) {
  if (const char *Name = unsupportedFloatType(I.getType()))
    return unsupported(I, Twine(Name) + " is not supported");
  for (Value *Op : I.operands())
    if (const char *Name = unsupportedFloatType(Op->getType()))
      return unsupported(I, Twine(Name) + " is not supported");
}

// Only the intrinsics that map to a library function (or an inline
// sequence) are supported on floating point values; constrained intrinsics
// carry rounding mode and exception semantics that the AWP does not have.
void LowerPointers::checkFloatIntrinsic(Instruction &I, const CallBase &Call) {
  auto *II = dyn_cast<IntrinsicInst>(&Call);
  if (!II)
    return;
  bool HasFloat = II->getType()->isFloatingPointTy();
  for (Value *Op : II->args())
    HasFloat |= Op->getType()->isFloatingPointTy();
  if (!HasFloat)
    return;
  switch (II->getIntrinsicID()) {
  case Intrinsic::fabs:
  case Intrinsic::copysign:
  case Intrinsic::sqrt:
  case Intrinsic::sin:
  case Intrinsic::cos:
  case Intrinsic::tan:
  case Intrinsic::asin:
  case Intrinsic::acos:
  case Intrinsic::atan:
  case Intrinsic::atan2:
  case Intrinsic::sinh:
  case Intrinsic::cosh:
  case Intrinsic::tanh:
  case Intrinsic::pow:
  case Intrinsic::powi:
  case Intrinsic::exp:
  case Intrinsic::exp2:
  case Intrinsic::exp10:
  case Intrinsic::log:
  case Intrinsic::log2:
  case Intrinsic::log10:
  case Intrinsic::floor:
  case Intrinsic::ceil:
  case Intrinsic::trunc:
  case Intrinsic::rint:
  case Intrinsic::nearbyint:
  case Intrinsic::round:
  case Intrinsic::roundeven:
  case Intrinsic::minnum:
  case Intrinsic::maxnum:
  case Intrinsic::fma:
  case Intrinsic::fmuladd:
  case Intrinsic::ldexp:
    return;
  default:
    break;
  }
  if (II->getCalledFunction()->getName().starts_with(
          "llvm.experimental.constrained."))
    return unsupported(I, "constrained floating point intrinsics are not "
                          "supported");
  unsupported(I, "the intrinsic " + II->getCalledFunction()->getName() +
                     " is not supported on floating point values");
}

namespace {
// A position inside a structure or array (the indices of an extractvalue), and
// a constant found there.
using LeafPath = SmallVector<unsigned, 4>;
struct FloatLeaf {
  LeafPath Path;
  ConstantFP *Value;
};

bool startsWith(const LeafPath &Path, const LeafPath &Prefix) {
  return Path.size() >= Prefix.size() &&
         std::equal(Prefix.begin(), Prefix.end(), Path.begin());
}

// Finds the floating point constants that an instruction selector would see
// as the elements of V without any computation in between: V itself, the
// elements of a constant structure or array, what insertvalue puts in, what
// extractvalue and freeze pass on. The paths are relative to V. A zero
// initialiser holds only (positive) zeros, which have the same bits in both
// formats, so it is skipped.
void findFloatLeaves(Value *V, SmallVectorImpl<FloatLeaf> &Out) {
  Type *Ty = V->getType();
  if (Ty->isDoubleTy()) {
    if (auto *C = dyn_cast<ConstantFP>(V)) {
      Out.push_back({{}, C});
    } else if (auto *Freeze = dyn_cast<FreezeInst>(V)) {
      findFloatLeaves(Freeze->getOperand(0), Out);
    } else if (auto *Extract = dyn_cast<ExtractValueInst>(V)) {
      SmallVector<FloatLeaf, 4> Inner;
      findFloatLeaves(Extract->getAggregateOperand(), Inner);
      LeafPath Wanted(Extract->getIndices());
      for (FloatLeaf &L : Inner)
        if (L.Path == Wanted)
          Out.push_back({{}, L.Value});
    }
    return;
  }
  if (!Ty->isStructTy() && !Ty->isArrayTy())
    return;
  if (auto *C = dyn_cast<Constant>(V)) {
    if (isa<ConstantAggregateZero>(C) || isa<UndefValue>(C))
      return;
    unsigned N = Ty->isStructTy() ? Ty->getStructNumElements()
                                  : Ty->getArrayNumElements();
    for (unsigned I = 0; I != N; ++I) {
      SmallVector<FloatLeaf, 4> Inner;
      if (Constant *Elt = C->getAggregateElement(I))
        findFloatLeaves(Elt, Inner);
      for (FloatLeaf &L : Inner) {
        L.Path.insert(L.Path.begin(), I);
        Out.push_back(std::move(L));
      }
    }
  } else if (auto *Insert = dyn_cast<InsertValueInst>(V)) {
    SmallVector<FloatLeaf, 4> Base;
    findFloatLeaves(Insert->getAggregateOperand(), Base);
    LeafPath At(Insert->getIndices());
    // What was inserted replaces everything that was there.
    for (FloatLeaf &L : Base)
      if (!startsWith(L.Path, At))
        Out.push_back(std::move(L));
    SmallVector<FloatLeaf, 4> Inner;
    findFloatLeaves(Insert->getInsertedValueOperand(), Inner);
    for (FloatLeaf &L : Inner) {
      L.Path.insert(L.Path.begin(), At.begin(), At.end());
      Out.push_back(std::move(L));
    }
  } else if (auto *Extract = dyn_cast<ExtractValueInst>(V)) {
    SmallVector<FloatLeaf, 4> Inner;
    findFloatLeaves(Extract->getAggregateOperand(), Inner);
    LeafPath At(Extract->getIndices());
    for (FloatLeaf &L : Inner)
      if (startsWith(L.Path, At)) {
        L.Path.erase(L.Path.begin(), L.Path.begin() + At.size());
        Out.push_back(std::move(L));
      }
  } else if (auto *Freeze = dyn_cast<FreezeInst>(V)) {
    findFloatLeaves(Freeze->getOperand(0), Out);
  }
}

// How many registers or stack words a value of this type takes when it is
// passed: a double is three (its padding word is not passed), a 64-bit
// integer four, and so on. Must agree with the calling convention.
unsigned passedWords(Type *Ty, const DataLayout &DL) {
  if (Ty->isDoubleTy())
    return 3;
  if (auto *IT = dyn_cast<IntegerType>(Ty))
    return std::max(1u, (IT->getBitWidth() + 15) / 16);
  if (auto *PT = dyn_cast<PointerType>(Ty))
    return std::max(1u, DL.getPointerSizeInBits(PT->getAddressSpace()) / 16);
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return AT->getNumElements() * passedWords(AT->getElementType(), DL);
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    unsigned Words = 0;
    for (Type *Elt : ST->elements())
      Words += passedWords(Elt, DL);
    return Words;
  }
  return 1;
}
} // namespace

// Some floating point constants are passed on as plain words, without the
// instruction selector ever seeing them as floating point values: call
// arguments, return values and phi inputs are split into words as soon as the
// selection DAG is built (and the generic code then reads the constant as an
// IEEE number), so they are converted here. The converted constant is the
// integer with the native bits, reinterpreted by an (unfolded) bitcast; the
// instruction selector folds it into a floating point constant that is only
// moved and never interpreted. Constants inside structures and arrays are
// found with findFloatLeaves and replaced by insertvalue instructions that put
// the converted constant in place (the original value stays as it was for its
// other users). Constants used by arithmetic, comparisons, stores and
// conversions are converted by the instruction selector instead, and so are
// the elements of a result that is too big for R1..R3, which the generic code
// stores to memory through a hidden pointer.
void LowerPointers::materializeFloatConstants(Instruction &I) {
  const DataLayout &DL = F.getDataLayout();
  if (auto *Ret = dyn_cast<ReturnInst>(&I)) {
    if (!Ret->getReturnValue() ||
        passedWords(Ret->getReturnValue()->getType(), DL) > 3)
      return;
  } else if (!isa<PHINode>(I) &&
             !(isa<CallBase>(I) && !isa<IntrinsicInst>(I))) {
    return;
  }
  for (Use &U : I.operands()) {
    SmallVector<FloatLeaf, 4> Leaves;
    findFloatLeaves(U.get(), Leaves);
    if (Leaves.empty())
      continue;
    Instruction *Where = &I;
    if (auto *Phi = dyn_cast<PHINode>(&I))
      Where = Phi->getIncomingBlock(U)->getTerminator();
    Value *New = U.get();
    for (FloatLeaf &L : Leaves) {
      uint16_t Words[3] = {0, 0, 0};
      Mera400::FloatConvStatus Status = Mera400::convertDoubleToNativeFloat(
          L.Value->getValueAPF().convertToDouble(), /*RoundToNearest=*/true,
          Words);
      if (Status == Mera400::FloatConvStatus::Underflow)
        Words[0] = Words[1] = Words[2] = 0;
      else if (Status != Mera400::FloatConvStatus::Ok)
        return unsupported(I, "a floating point constant is not representable "
                              "in the MERA-400 floating point format "
                              "(infinity, NaN, or outside about 1e-38 .. "
                              "1e38)");
      uint64_t Bits = (uint64_t(Words[0]) << 48) | (uint64_t(Words[1]) << 32) |
                      (uint64_t(Words[2]) << 16);
      Value *Native = new BitCastInst(
          ConstantInt::get(Type::getInt64Ty(F.getContext()), Bits),
          L.Value->getType(), "", Where->getIterator());
      if (L.Path.empty())
        New = Native;
      else
        New = InsertValueInst::Create(New, Native, L.Path, "",
                                      Where->getIterator());
    }
    U.set(New);
    Changed = true;
  }
}

// First scan the function and report every unsupported construct, collecting
// the instructions to rewrite; rewrite only if nothing failed.
// A 16-bit multiplication with overflow becomes a 32-bit multiplication (a
// library call) whose high word is checked. The generic expansion in the
// instruction selector cannot be used: it calls the 32-bit multiplication with
// four separate 16-bit arguments (high and low words of both operands), but a
// 32-bit argument that would be split between R3 and the stack goes to the
// stack as a whole (see analyzeArgs in Mera400ISelLowering.cpp), so the callee
// would read other words.
static void expandMulWithOverflow(IntrinsicInst &II) {
  bool Signed = II.getIntrinsicID() == Intrinsic::smul_with_overflow;
  IRBuilder<> B(&II);
  Type *Wide = B.getInt32Ty();
  Value *X = Signed ? B.CreateSExt(II.getArgOperand(0), Wide)
                    : B.CreateZExt(II.getArgOperand(0), Wide);
  Value *Y = Signed ? B.CreateSExt(II.getArgOperand(1), Wide)
                    : B.CreateZExt(II.getArgOperand(1), Wide);
  Value *Product = B.CreateMul(X, Y);
  Value *Low = B.CreateTrunc(Product, B.getInt16Ty());
  // Unsigned: any bit above the low word is an overflow. Signed: the product
  // must equal the sign extension of its low word.
  Value *Back = Signed ? B.CreateSExt(Low, Wide) : B.CreateZExt(Low, Wide);
  Value *Overflow = B.CreateICmpNE(Product, Back);
  Value *Result = PoisonValue::get(II.getType());
  Result = B.CreateInsertValue(Result, Low, 0);
  Result = B.CreateInsertValue(Result, Overflow, 1);
  II.replaceAllUsesWith(Result);
  II.eraseFromParent();
}

bool LowerPointers::run() {
  {
    SmallVector<IntrinsicInst *> Mulo;
    for (Instruction &I : instructions(F))
      if (auto *II = dyn_cast<IntrinsicInst>(&I))
        if ((II->getIntrinsicID() == Intrinsic::umul_with_overflow ||
             II->getIntrinsicID() == Intrinsic::smul_with_overflow) &&
            II->getArgOperand(0)->getType()->isIntegerTy(16))
          Mulo.push_back(II);
    for (IntrinsicInst *II : Mulo)
      expandMulWithOverflow(*II);
  }
  expandConstantGEPs();
  splitAggregateAccesses();
  // At -O0 Clang leaves "extractvalue" of a constant aggregate (a
  // pointer-to-member-function constant) in the IR, and splitting a store of
  // such an aggregate creates more. When an element is a pointer-to-integer
  // constant expression, the type legalizer cannot expand it ("do not know
  // how to expand the result of this operator"): the scan below only
  // rewrites constant expressions that are direct instruction operands. Take
  // the element directly, before that scan.
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *Extract = dyn_cast<ExtractValueInst>(&I);
    auto *Agg = Extract ? dyn_cast<Constant>(Extract->getAggregateOperand())
                        : nullptr;
    if (!Agg)
      continue;
    Constant *Element = Agg;
    for (unsigned Index : Extract->indices())
      Element = Element ? Element->getAggregateElement(Index) : nullptr;
    if (!Element)
      continue;
    Extract->replaceAllUsesWith(Element);
    Extract->eraseFromParent();
  }
  expandConstantGEPs();
  widenBoolAccesses();
  retargetToBytePointers();

  // Memory and variable argument intrinsics first: they are replaced by
  // ordinary loads, stores, casts and calls, which the scan below then checks
  // like any other.
  {
    SmallVector<Instruction *> Early;
    for (Instruction &I : instructions(F))
      if (isa<MemIntrinsic>(I) || isa<VAArgInst>(I) || isa<VAStartInst>(I) ||
          isa<VAEndInst>(I) || isa<VACopyInst>(I))
        Early.push_back(&I);
    for (Instruction *I : Early) {
      if (auto *MI = dyn_cast<MemIntrinsic>(I))
        lowerMemIntrinsic(*MI);
      else
        lowerVarArgs(*I);
    }
  }

  bool Large = isLarge();
  SmallVector<Instruction *> ByteAccesses, WordAccesses;
  SmallVector<GetElementPtrInst *> GEPs;
  SmallVector<Instruction *> FloatConstantUsers;
  for (Argument &A : F.args())
    if (const char *Name = unsupportedFloatType(A.getType()))
      unsupported(F.getEntryBlock().front(), Twine(Name) + " is not supported");
  if (const char *Name = unsupportedFloatType(F.getReturnType()))
    unsupported(F.getEntryBlock().front(), Twine(Name) + " is not supported");
  for (Argument &A : F.args())
    if (A.getType()->isIntegerTy() && A.getType()->getIntegerBitWidth() > 64)
      unsupported(F.getEntryBlock().front(),
                  "integers wider than 64 bits are not supported");
  if (F.getReturnType()->isIntegerTy() &&
      F.getReturnType()->getIntegerBitWidth() > 64)
    unsupported(F.getEntryBlock().front(),
                "integers wider than 64 bits are not supported");
  for (Instruction &I : instructions(F)) {
    bool Wide = I.getType()->isIntegerTy() &&
                I.getType()->getIntegerBitWidth() > 64;
    for (Value *Op : I.operands())
      Wide |= Op->getType()->isIntegerTy() &&
              Op->getType()->getIntegerBitWidth() > 64;
    if (Wide)
      unsupported(I, "integers wider than 64 bits are not supported");
    checkFloatTypes(I);
    if (auto *Call = dyn_cast<CallBase>(&I))
      checkFloatIntrinsic(I, *Call);
    if (isa<ReturnInst>(I) || isa<PHINode>(I) || isa<CallBase>(I))
      FloatConstantUsers.push_back(&I);
    for (Value *Op : I.operands())
      if (auto *GV = dyn_cast<GlobalValue>(Op))
        if (GV->getAddressSpace() != 0 &&
            !(isa<Function>(GV) && cast<Function>(GV)->isIntrinsic()))
          unsupported(I, "global variables must be in address space 0");
    if (auto *L = dyn_cast<LoadInst>(&I)) {
      checkAccess(I, L->getType(), L->getPointerOperand(), L->getAlign(),
                  L->isAtomic());
      if (L->getPointerAddressSpace() == 1) {
        if (!L->getType()->isIntegerTy(8))
          WordAccesses.push_back(&I);
        else if (Large)
          ByteAccesses.push_back(&I);
      }
    } else if (auto *S = dyn_cast<StoreInst>(&I)) {
      checkAccess(I, S->getValueOperand()->getType(), S->getPointerOperand(),
                  S->getAlign(), S->isAtomic());
      if (S->getPointerAddressSpace() == 1) {
        if (!S->getValueOperand()->getType()->isIntegerTy(8))
          WordAccesses.push_back(&I);
        else if (Large)
          ByteAccesses.push_back(&I);
      }
    } else if (isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I)) {
      unsupported(I, "atomic operations are not supported");
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
      GEPs.push_back(GEP);
    } else if (auto *ASC = dyn_cast<AddrSpaceCastInst>(&I)) {
      if (ASC->getSrcAddressSpace() > 1 || ASC->getDestAddressSpace() > 1)
        unsupported(I, "address space casts are only supported between "
                       "address spaces 0 and 1");
    } else if (isa<AnyMemIntrinsic>(I)) {
      unsupported(I, "atomic memory copy and fill operations are not "
                     "supported");
    } else if (isa<IndirectBrInst>(I) || isa<CallBrInst>(I)) {
      unsupported(I, "indirect branches (indirectbr, callbr) are not "
                     "supported");
    }
    for (Value *Op : I.operands())
      if (isa<BlockAddress>(Op))
        unsupported(I, "the address of a basic block (blockaddress) is not "
                       "supported");
  }
  for (GetElementPtrInst *GEP : GEPs)
    lowerGEP(*GEP);
  if (Failed) {
    discardBody();
    return true;
  }
  for (Instruction *I : FloatConstantUsers)
    materializeFloatConstants(*I);
  if (Failed) {
    discardBody();
    return true;
  }
  for (Instruction *I : WordAccesses)
    lowerWordAccess(*I);
  for (Instruction *I : ByteAccesses)
    lowerByteAccess(*I);
  if (Large) {
    SmallVector<AddrSpaceCastInst *> Casts;
    for (Instruction &I : instructions(F))
      if (auto *ASC = dyn_cast<AddrSpaceCastInst>(&I))
        if (ASC->getSrcAddressSpace() != ASC->getDestAddressSpace())
          Casts.push_back(ASC);
    for (AddrSpaceCastInst *ASC : Casts)
      lowerLargeAddrSpaceCast(*ASC);
  }
  return Changed;
}

namespace {

class Mera400LowerPointersLegacy : public FunctionPass {
public:
  static char ID;
  Mera400LowerPointersLegacy() : FunctionPass(ID) {}

  StringRef getPassName() const override {
    return "MERA-400 pointer lowering";
  }

  bool runOnFunction(Function &F) override { return LowerPointers(F).run(); }
};

} // end anonymous namespace

char Mera400LowerPointersLegacy::ID = 0;

INITIALIZE_PASS(Mera400LowerPointersLegacy, DEBUG_TYPE,
                "MERA-400 pointer lowering", false, false)

FunctionPass *llvm::createMera400LowerPointersLegacyPass() {
  return new Mera400LowerPointersLegacy();
}

PreservedAnalyses Mera400LowerPointersPass::run(Function &F,
                                                FunctionAnalysisManager &) {
  return LowerPointers(F).run() ? PreservedAnalyses::none()
                                : PreservedAnalyses::all();
}
