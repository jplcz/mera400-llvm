//===-- Mera400AsmPrinter.cpp - MERA-400 assembly printer
//-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Prints compiled code as assembly or objects: the AsmPrinter (the LLVM class
// that turns MachineInstrs and globals into MC (machine code layer) output)
// for this target. It also converts constant pointers in data to the right
// units, because a word pointer holds a word address while ELF symbol values
// are byte addresses.
//
//===----------------------------------------------------------------------===//

#include "Mera400AsmPrinter.h"
#include "Mera400.h"
#include "Mera400MCInstLower.h"
#include "MCTargetDesc/Mera400FloatFormat.h"
#include "MCTargetDesc/Mera400MCExpr.h"
#include "MCTargetDesc/Mera400TargetStreamer.h"
#include "Mera400TargetMachine.h"
#include "TargetInfo/Mera400TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/AsmPrinterAnalysis.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Operator.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {
class Mera400AsmPrinter : public AsmPrinter {
public:
  Mera400AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "MERA-400 Assembly Printer"; }

  bool runOnMachineFunction(MachineFunction &MF) override {
    SetupMachineFunction(MF);
    emitFunctionBody();
    return false;
  }

  void emitInstruction(const MachineInstr *MI) override;
  void emitStartOfAsmFile(Module &M) override;
  void emitGlobalVariable(const GlobalVariable *GV) override;
  const MCExpr *lowerConstant(const Constant *CV,
                              const Constant *BaseCV = nullptr,
                              uint64_t Offset = 0) override;
  const MCExpr *lowerWordPointerBytes(const Constant *C);
  const MCExpr *lowerBytePointer(const Constant *C);

  static char ID;
};
} // end anonymous namespace

// Writes the memory model (".model small" or ".model large") first, so the
// object file records which pointer width it was compiled for.
void Mera400AsmPrinter::emitStartOfAsmFile(Module &M) {
  // The module data layout is the single source of truth for the model; the
  // feature, when given, must agree with it.
  bool Large = M.getDataLayout().getPointerSizeInBits(1) == 32;
  if (!Large && TM.getMCSubtargetInfo().checkFeatures("+large-memory"))
    reportFatalUsageError(
        "Mera400: the 'large-memory' feature requires a module data layout "
        "with 32-bit address space 1 pointers (p1:32:16), but the module has "
        "the small memory model layout");
  if (auto *TS = static_cast<Mera400TargetStreamer *>(
          OutStreamer->getTargetStreamer()))
    TS->emitCompilerModel(Large);
}

// Pseudos are lowered to real MC instructions (see Mera400MCInstLower.cpp);
// one pseudo may produce several.
void Mera400AsmPrinter::emitInstruction(const MachineInstr *MI) {
  SmallVector<MCInst, 2> Insts;
  Mera400MCInstLower(*this).lower(MI, Insts);
  for (const MCInst &Inst : Insts)
    EmitToStreamer(*OutStreamer, Inst);
}

// Initialisers that contain doubles are written in the native format. The
// generic AsmPrinter code that emits floating point constants is not
// overridable and would write IEEE bits, so a copy of the variable is emitted
// instead, in which every double is replaced by the i64 that holds the native
// bits (three words and a zero padding word, high word first: the same four
// words the double occupies in memory). Types change only where they contain
// a double; sizes and alignments stay the same (i64 and double both have
// 16-bit alignment in the data layout).
static Type *nativeFloatType(Type *Ty) {
  LLVMContext &Ctx = Ty->getContext();
  if (Ty->isDoubleTy())
    return Type::getInt64Ty(Ctx);
  if (auto *AT = dyn_cast<ArrayType>(Ty)) {
    Type *Elt = nativeFloatType(AT->getElementType());
    return Elt == AT->getElementType()
               ? Ty
               : ArrayType::get(Elt, AT->getNumElements());
  }
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    SmallVector<Type *> Elts;
    bool Changed = false;
    for (Type *Elt : ST->elements()) {
      Elts.push_back(nativeFloatType(Elt));
      Changed |= Elts.back() != Elt;
    }
    return Changed ? StructType::get(Ctx, Elts, ST->isPacked()) : Ty;
  }
  return Ty;
}

static uint64_t nativeFloatBits(const APFloat &Value, LLVMContext &Ctx) {
  uint16_t Words[3] = {0, 0, 0};
  Mera400::FloatConvStatus Status = Mera400::convertDoubleToNativeFloat(
      Value.convertToDouble(), /*RoundToNearest=*/true, Words);
  if (Status == Mera400::FloatConvStatus::Underflow) {
    Words[0] = Words[1] = Words[2] = 0;
  } else if (Status != Mera400::FloatConvStatus::Ok) {
    Ctx.emitError("Mera400: a floating point constant in a global variable "
                  "is not representable in the MERA-400 floating point format "
                  "(infinity, NaN, or outside about 1e-38 .. 1e38)");
    Words[0] = Words[1] = Words[2] = 0;
  }
  return (uint64_t(Words[0]) << 48) | (uint64_t(Words[1]) << 32) |
         (uint64_t(Words[2]) << 16);
}

static Constant *nativeFloatConstant(Constant *C) {
  Type *NewTy = nativeFloatType(C->getType());
  if (NewTy == C->getType())
    return C;
  LLVMContext &Ctx = C->getContext();
  if (auto *CF = dyn_cast<ConstantFP>(C))
    return ConstantInt::get(NewTy, nativeFloatBits(CF->getValueAPF(), Ctx));
  if (isa<ConstantAggregateZero>(C))
    return ConstantAggregateZero::get(NewTy);
  if (isa<PoisonValue>(C))
    return PoisonValue::get(NewTy);
  if (isa<UndefValue>(C))
    return UndefValue::get(NewTy);
  if (auto *CDA = dyn_cast<ConstantDataArray>(C)) {
    SmallVector<uint64_t> Bits;
    for (unsigned I = 0, E = CDA->getNumElements(); I != E; ++I)
      Bits.push_back(nativeFloatBits(CDA->getElementAsAPFloat(I), Ctx));
    return ConstantDataArray::get(Ctx, Bits);
  }
  SmallVector<Constant *> Elts;
  for (Value *Op : C->operands())
    Elts.push_back(nativeFloatConstant(cast<Constant>(Op)));
  if (auto *ST = dyn_cast<StructType>(NewTy))
    return ConstantStruct::get(ST, Elts);
  return ConstantArray::get(cast<ArrayType>(NewTy), Elts);
}

void Mera400AsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  // The "llvm." variables (the constructor list) are not data, and in the
  // large model they are in address space 1 along with the other globals the
  // front end creates, see Mera400LowerGlobalSpaces.cpp.
  if (GV->getAddressSpace() != 0 && !GV->getName().starts_with("llvm.")) {
    GV->getContext().emitError(
        "Mera400: global variables must be in address space 0, because the "
        "address of a global is a word address");
    return;
  }
  // The address of a global is a word address, so every global must start on
  // a word boundary. Front ends give char arrays and string literals an
  // alignment of 1, which would let the linker place them at an odd byte.
  // Without an explicit alignment the data layout's preferred alignment is
  // kept when it is already at least 2 (large arrays get more).
  Align Wanted = GV->getAlign().value_or(
      GV->getDataLayout().getPreferredAlign(GV));
  if (Wanted < Align(2))
    const_cast<GlobalVariable *>(GV)->setAlignment(Align(2));
  if (const char *Why = getUnsupportedFloatType(GV->getValueType())) {
    GV->getContext().emitError(Twine("Mera400: ") + Why +
                               " is not supported (global variable " +
                               GV->getName() + ")");
    return;
  }
  if (!GV->hasInitializer() ||
      nativeFloatType(GV->getValueType()) == GV->getValueType()) {
    AsmPrinter::emitGlobalVariable(GV);
    return;
  }

  // The copy takes over the name (and so the symbol) of the original for the
  // duration of the emission.
  auto *Orig = const_cast<GlobalVariable *>(GV);
  Module &M = *Orig->getParent();
  std::string Name = Orig->getName().str();
  Orig->setName(Name + ".mera400.original");
  Constant *Init = nativeFloatConstant(Orig->getInitializer());
  auto *Copy = new GlobalVariable(M, Init->getType(), Orig->isConstant(),
                                  Orig->getLinkage(), Init, Name);
  Copy->copyAttributesFrom(Orig);
  Copy->copyMetadata(Orig, 0);
  AsmPrinter::emitGlobalVariable(Copy);
  Copy->eraseFromParent();
  Orig->setName(Name);
}

// The ELF symbol values of globals and functions are byte addresses, but a
// word pointer (address space 0) holds a word address. Constant pointers in
// data (vtables, function pointer tables, linked structures) are therefore
// emitted as "words(byte address)", which becomes a WORD16 relocation. A
// constant "getelementptr" computes bytes and must be a whole number of words.
const MCExpr *Mera400AsmPrinter::lowerWordPointerBytes(const Constant *C) {
  auto *CE = dyn_cast<ConstantExpr>(C);
  if (!CE || CE->getOpcode() != Instruction::GetElementPtr)
    return AsmPrinter::lowerConstant(C);

  APInt Bytes(getDataLayout().getIndexTypeSizeInBits(CE->getType()), 0);
  if (!cast<GEPOperator>(CE)->accumulateConstantOffset(getDataLayout(),
                                                       Bytes) ||
      Bytes[0]) {
    CE->getContext().emitError("Mera400: a word pointer can only be offset by "
                               "a whole number of 16-bit words");
    return MCConstantExpr::create(0, OutContext);
  }
  const MCExpr *Base = lowerWordPointerBytes(CE->getOperand(0));
  if (!Bytes)
    return Base;
  return MCBinaryExpr::createAdd(
      Base, MCConstantExpr::create(Bytes.getSExtValue(), OutContext),
      OutContext);
}

// A byte pointer (address space 1, large model) holds a byte address, which is
// what the ELF symbol values are.
const MCExpr *Mera400AsmPrinter::lowerBytePointer(const Constant *C) {
  auto *CE = dyn_cast<ConstantExpr>(C);
  if (!CE)
    return AsmPrinter::lowerConstant(C);
  if (CE->getOpcode() == Instruction::AddrSpaceCast) {
    const Constant *Op = CE->getOperand(0);
    return Op->getType()->getPointerAddressSpace() == 0
               ? lowerWordPointerBytes(Op)
               : lowerBytePointer(Op);
  }
  if (CE->getOpcode() != Instruction::GetElementPtr)
    return AsmPrinter::lowerConstant(C);
  APInt Bytes(getDataLayout().getIndexTypeSizeInBits(CE->getType()), 0);
  if (!cast<GEPOperator>(CE)->accumulateConstantOffset(getDataLayout(),
                                                       Bytes)) {
    CE->getContext().emitError("Mera400: cannot compute the offset of this "
                               "constant getelementptr");
    return MCConstantExpr::create(0, OutContext);
  }
  const MCExpr *Base = lowerBytePointer(CE->getOperand(0));
  if (!Bytes)
    return Base;
  return MCBinaryExpr::createAdd(
      Base, MCConstantExpr::create(Bytes.getSExtValue(), OutContext),
      OutContext);
}

// Chooses the unit for each constant pointer: a word pointer or the result of
// "ptrtoint" of one is a word address (WORD16 relocation); a byte pointer
// (address space 1, 16 bits in the small model and 32 in the large one) is a
// byte address, which is what the ELF symbol values are, so a C pointer to a
// global ("addrspacecast" of the global) in a global initialiser is the
// symbol itself; a word pointer made by casting from a byte pointer is that
// byte address converted to words.
const MCExpr *Mera400AsmPrinter::lowerConstant(const Constant *CV,
                                               const Constant *BaseCV,
                                               uint64_t Offset) {
  const Constant *Pointer = nullptr;
  if (CV->getType()->isPointerTy() &&
      CV->getType()->getPointerAddressSpace() == 1) {
    if (isa<ConstantExpr>(CV))
      return lowerBytePointer(CV);
    return AsmPrinter::lowerConstant(CV, BaseCV, Offset);
  }
  if (isa<AddrSpaceCastOperator>(CV) &&
      CV->getType()->getPointerAddressSpace() == 0)
    return Mera400HalfExpr::create(
        lowerBytePointer(cast<Constant>(cast<Operator>(CV)->getOperand(0))),
        OutContext);
  if (CV->getType()->isPointerTy() &&
      CV->getType()->getPointerAddressSpace() == 0) {
    if (isa<GlobalValue>(CV) || isa<GEPOperator>(CV))
      Pointer = CV;
  } else if (auto *CE = dyn_cast<ConstantExpr>(CV)) {
    // "ptrtoint" of a word pointer is a word address.
    if (CE->getOpcode() == Instruction::PtrToInt &&
        CE->getOperand(0)->getType()->getPointerAddressSpace() == 0 &&
        (isa<GlobalValue>(CE->getOperand(0)) ||
         isa<GEPOperator>(CE->getOperand(0))))
      Pointer = CE->getOperand(0);
  }
  if (!Pointer)
    return AsmPrinter::lowerConstant(CV, BaseCV, Offset);
  return Mera400HalfExpr::create(lowerWordPointerBytes(Pointer), OutContext);
}

char Mera400AsmPrinter::ID = 0;

INITIALIZE_PASS(Mera400AsmPrinter, "mera400-asm-printer",
                "MERA-400 Assembly Printer", false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMera400AsmPrinter() {
  RegisterAsmPrinter<Mera400AsmPrinter> X(getTheMera400Target());
}

PreservedAnalyses Mera400AsmPrinterBeginPass::run(Module &M,
                                                  ModuleAnalysisManager &MAM) {
  Mera400AsmPrinter &AsmPrinter = static_cast<Mera400AsmPrinter &>(
      MAM.getResult<AsmPrinterAnalysis>(M).getPrinter());
  setupModuleAsmPrinter(M, MAM, AsmPrinter);
  AsmPrinter.doInitialization(M);
  return PreservedAnalyses::all();
}

PreservedAnalyses
Mera400AsmPrinterPass::run(MachineFunction &MF,
                           MachineFunctionAnalysisManager &MFAM) {
  Mera400AsmPrinter &AsmPrinter = static_cast<Mera400AsmPrinter &>(
      MFAM.getResult<ModuleAnalysisManagerMachineFunctionProxy>(MF)
          .getCachedResult<AsmPrinterAnalysis>(*MF.getFunction().getParent())
          ->getPrinter());
  setupMachineFunctionAsmPrinter(MFAM, MF, AsmPrinter);
  AsmPrinter.runOnMachineFunction(MF);
  return PreservedAnalyses::all();
}

PreservedAnalyses Mera400AsmPrinterEndPass::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  Mera400AsmPrinter &AsmPrinter = static_cast<Mera400AsmPrinter &>(
      MAM.getResult<AsmPrinterAnalysis>(M).getPrinter());
  setupModuleAsmPrinter(M, MAM, AsmPrinter);
  AsmPrinter.doFinalization(M);
  return PreservedAnalyses::all();
}
