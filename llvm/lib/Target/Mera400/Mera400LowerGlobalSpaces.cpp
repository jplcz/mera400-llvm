//===-- Mera400LowerGlobalSpaces.cpp - Code and runtime data pointers -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// In the large memory model the data layout names address space 1 (32-bit
// byte pointers) as the "program" space (layout field P, where functions
// live) and as the default space for globals (field G, where Clang puts the
// vtables of C++ classes). Clang lays out a function pointer, a virtual table
// pointer and a virtual table slot as the size of a C data pointer, 4 bytes,
// in the large model, and the Itanium C++ ABI code that makes pointers to
// member functions and virtual calls takes its sizes from the same source, so
// these two layout fields are the only way to make the front end and the back
// end agree without changing generic Clang code.
//
// The machine itself only knows word addresses, so this pass, which runs
// before everything else in the back end, gives every such function and
// global its real, address space 0 definition and turns the address space 1
// pointer to it into "addrspacecast (ptr @definition to ptr addrspace(1))":
// the byte address of the object, the same unit as an ELF symbol value. Direct
// calls keep calling the address space 0 function. After the pass the rest of
// the back end sees only what it already supports: word pointers, casts to
// byte pointers and 32-bit byte addresses in data (".long symbol").
//
// Intrinsic functions stay where they are (a call to an intrinsic is never
// selected as a call) and so do the "llvm." global variables, which the
// assembly printer finds by name.
//
//===----------------------------------------------------------------------===//

#include "Mera400.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "mera400-lower-global-spaces"

namespace {

bool lowerGlobalSpaces(Module &M) {
  // Only the large model has a distinct address space 1 representation; in the
  // small model the layout keeps functions and globals in address space 0.
  if (M.getDataLayout().getPointerSizeInBits(1) != 32)
    return false;

  SmallVector<GlobalVariable *> Vars;
  SmallVector<Function *> Functions;
  for (GlobalVariable &GV : M.globals())
    if (GV.getAddressSpace() != 0 && !GV.getName().starts_with("llvm."))
      Vars.push_back(&GV);
  for (Function &F : M)
    if (F.getAddressSpace() != 0 && !F.isIntrinsic())
      Functions.push_back(&F);
  for (GlobalAlias &A : M.aliases())
    if (A.getAddressSpace() != 0)
      M.getContext().diagnose(DiagnosticInfoGeneric(
          "Mera400: the alias " + A.getName() +
              " is in address space 1, which is not supported",
          DS_Error));

  for (GlobalVariable *GV : Vars) {
    std::string Name = GV->getName().str();
    GV->setName("");
    auto *New = new GlobalVariable(
        M, GV->getValueType(), GV->isConstant(), GV->getLinkage(), nullptr,
        Name, GV, GV->getThreadLocalMode(), /*AddressSpace=*/0,
        GV->isExternallyInitialized());
    New->copyAttributesFrom(GV);
    New->copyMetadata(GV, 0);
    if (GV->hasInitializer()) {
      New->setInitializer(GV->getInitializer());
      GV->setInitializer(nullptr);
    }
    GV->replaceAllUsesWith(ConstantExpr::getAddrSpaceCast(New, GV->getType()));
    GV->eraseFromParent();
  }

  for (Function *F : Functions) {
    std::string Name = F->getName().str();
    F->setName("");
    // Created with its final name so that a function that is a known library
    // function or has other name-based properties keeps them.
    Function *New = Function::Create(F->getFunctionType(), F->getLinkage(),
                                     /*AddrSpace=*/0, Name);
    M.getFunctionList().insert(F->getIterator(), New);
    New->copyAttributesFrom(F);
    New->copyMetadata(F, 0);
    New->splice(New->begin(), F);
    for (auto [Old, Replacement] : zip(F->args(), New->args())) {
      Replacement.takeName(&Old);
      Old.replaceAllUsesWith(&Replacement);
    }
    // A direct call stays direct: only the other uses (the function's
    // address) need the byte address.
    for (Use &U : make_early_inc_range(F->uses()))
      if (auto *Call = dyn_cast<CallBase>(U.getUser()))
        if (Call->isCallee(&U))
          Call->setCalledFunction(New);
    F->replaceAllUsesWith(ConstantExpr::getAddrSpaceCast(New, F->getType()));
    F->eraseFromParent();
  }
  return !Vars.empty() || !Functions.empty();
}

class Mera400LowerGlobalSpacesLegacy : public ModulePass {
public:
  static char ID;
  Mera400LowerGlobalSpacesLegacy() : ModulePass(ID) {}

  StringRef getPassName() const override {
    return "MERA-400 global address space lowering";
  }

  bool runOnModule(Module &M) override { return lowerGlobalSpaces(M); }
};

} // end anonymous namespace

char Mera400LowerGlobalSpacesLegacy::ID = 0;

INITIALIZE_PASS(Mera400LowerGlobalSpacesLegacy, DEBUG_TYPE,
                "MERA-400 global address space lowering", false, false)

ModulePass *llvm::createMera400LowerGlobalSpacesLegacyPass() {
  return new Mera400LowerGlobalSpacesLegacy();
}

PreservedAnalyses Mera400LowerGlobalSpacesPass::run(Module &M,
                                                    ModuleAnalysisManager &) {
  return lowerGlobalSpaces(M) ? PreservedAnalyses::none()
                              : PreservedAnalyses::all();
}
