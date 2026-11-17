//===-- Mera400.h - Top-level interface for the MERA-400 backend -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Target-wide declarations shared by the backend: immediate range helpers,
// condition codes for the fused compare-and-branch pseudos, and the factory
// functions for the backend's passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400_H
#define LLVM_LIB_TARGET_MERA400_MERA400_H

#include "MCTargetDesc/Mera400MCTargetDesc.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
namespace Mera400 {
// "lwt" and "awt" carry a sign-magnitude 6-bit value in the instruction word
// itself, saving the extra word the general form needs.
inline bool isShortImm(int64_t Value) { return Value >= -63 && Value <= 63; }

// Condition of a compare-and-branch (BR_CC) or compare-and-select. Each
// condition and its inverse differ only in the lowest bit.
enum CondCode {
  CC_EQ = 0,
  CC_NE = 1,
  CC_LT = 2,
  CC_GE = 3,
  CC_GT = 4,
  CC_LE = 5,
  CC_ULT = 6,
  CC_UGE = 7,
  CC_UGT = 8,
  CC_ULE = 9
};
inline CondCode invertCC(CondCode CC) { return CondCode(CC ^ 1); }

// Selects the one-word short jumps (jls, jes, ...) where they exist; when
// false only the two-word long jumps are emitted. Branch relaxation and the
// instruction size estimates depend on this switch.
inline constexpr bool UseShortJumps = true;
inline bool isUnsignedCC(CondCode CC) { return CC >= CC_ULT; }
// A compare against a constant that fits "cwt" (a signed comparison).
inline bool usesShortCompare(CondCode CC, int64_t Imm) {
  return !isUnsignedCC(CC) && isShortImm(Imm);
}
} // namespace Mera400

class FunctionPass;
class ModulePass;
class Function;
class Type;
class Mera400TargetMachine;
class PassRegistry;

class Mera400ISelDAGToDAGPass : public SelectionDAGISelPass {
public:
  Mera400ISelDAGToDAGPass(Mera400TargetMachine &TM, CodeGenOptLevel OptLevel);
};

// Rewrites word pointer arithmetic into 16-bit word units and rejects memory
// accesses the backend cannot implement.
class Mera400LowerPointersPass
    : public RequiredPassInfoMixin<Mera400LowerPointersPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

FunctionPass *createMera400LowerPointersLegacyPass();

// Large memory model: moves the functions and globals the data layout places
// in address space 1 (function pointers and virtual tables are 32 bits there,
// as Clang lays them out) back to address space 0, see
// Mera400LowerGlobalSpaces.cpp. It runs before Mera400LowerPointersPass.
class Mera400LowerGlobalSpacesPass
    : public RequiredPassInfoMixin<Mera400LowerGlobalSpacesPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

ModulePass *createMera400LowerGlobalSpacesLegacyPass();

// Runs at the start of the optimization pipeline (registered by the target
// machine): makes the loads and stores of an object that is accessed both as a
// double and as another type volatile, so that the optimizer cannot treat the
// words of its native memory format as an IEEE bit pattern. See
// Mera400PunnedFloats.cpp.
class Mera400PunnedFloatsPass
    : public RequiredPassInfoMixin<Mera400PunnedFloatsPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

// Why a type cannot be used (a floating point type other than double, or a
// vector of floating point values), or null when it is fine. Looks inside
// arrays and structures.
const char *getUnsupportedFloatType(Type *Ty);
FunctionPass *createMera400ISelDag(Mera400TargetMachine &TM,
                                   CodeGenOptLevel OptLevel);

void initializeMera400AsmPrinterPass(PassRegistry &);
void initializeMera400DAGToDAGISelLegacyPass(PassRegistry &);
void initializeMera400LowerPointersLegacyPass(PassRegistry &);
void initializeMera400LowerGlobalSpacesLegacyPass(PassRegistry &);
} // namespace llvm

#endif
