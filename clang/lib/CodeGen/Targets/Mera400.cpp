//===- Mera400.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MERA-400 calling convention as seen by Clang. It has to agree with
// llvm/lib/Target/Mera400 (see docs/design/mera400-llvm.md, slice 7):
//   * integers narrower than a word are extended to a word, wider ones and
//     pointers pass directly, floating point values pass as an LLVM double;
//   * structs, unions, arrays and complex numbers are passed and returned
//     indirectly, as the address of a copy: the back end has no "byval";
//   * all variadic arguments live in one stack area of 16-bit slots and a
//     va_list is a byte pointer to the next slot.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {

class Mera400ABIInfo : public ABIInfo {
  // C data pointers are byte pointers in the default language address space,
  // which this target maps to LLVM address space 1. A returned aggregate is
  // written through such a pointer, because its destination can be anywhere
  // (heap, global, another function's frame).
  unsigned dataPointerAS() const {
    return getContext().getTargetAddressSpace(LangAS::Default);
  }

  // Indirect arguments are different: Clang's call lowering requires them to
  // be in the alloca address space (they are always a copy in the caller's
  // frame), which here is address space 0, the cheap word pointer. The callee
  // casts it to a byte pointer where C code needs one.
  unsigned argumentPointerAS() const {
    return getDataLayout().getAllocaAddrSpace();
  }

public:
  Mera400ABIInfo(CodeGenTypes &CGT) : ABIInfo(CGT) {}

  ABIArgInfo classifyArgumentType(QualType Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);

    if (isAggregateTypeForABI(Ty))
      return getNaturalAlignIndirect(Ty, argumentPointerAS(), /*ByVal=*/false);

    if (const auto *ED = Ty->getAsEnumDecl())
      Ty = ED->getIntegerType();

    // _BitInt wider than the largest integer type has no register form.
    if (const auto *EIT = Ty->getAs<BitIntType>())
      if (EIT->getNumBits() > getContext().getTypeSize(getContext().LongLongTy))
        return getNaturalAlignIndirect(Ty, argumentPointerAS(),
                                       /*ByVal=*/false);

    return isPromotableIntegerTypeForABI(Ty)
               ? ABIArgInfo::getExtend(Ty, CGT.ConvertType(Ty))
               : ABIArgInfo::getDirect();
  }

  ABIArgInfo classifyReturnType(QualType RetTy) const {
    if (RetTy->isVoidType())
      return ABIArgInfo::getIgnore();

    if (isAggregateTypeForABI(RetTy))
      return getNaturalAlignIndirect(RetTy, dataPointerAS(), /*ByVal=*/false);

    if (const auto *ED = RetTy->getAsEnumDecl())
      RetTy = ED->getIntegerType();

    if (const auto *EIT = RetTy->getAs<BitIntType>())
      if (EIT->getNumBits() > getContext().getTypeSize(getContext().LongLongTy))
        return getNaturalAlignIndirect(RetTy, dataPointerAS(),
                                       /*ByVal=*/false);

    return isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                                : ABIArgInfo::getDirect();
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    ASTContext &Ctx = getContext();
    bool IsIndirect = isAggregateTypeForABI(Ty);
    TypeInfoChars Info = Ctx.getTypeInfoInChars(Ty);
    // A floating point value occupies three words in the argument area: the
    // fourth word of its memory format is zero padding that is not passed.
    if (!IsIndirect && Ty->isRealFloatingType())
      Info.Width = CharUnits::fromQuantity(6);
    // A word is the only slot size; nothing needs more alignment than that.
    return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, Info,
                            CharUnits::fromQuantity(2),
                            /*AllowHigherAlign=*/false, Slot);
  }
};

class Mera400TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  Mera400TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<Mera400ABIInfo>(CGT)) {}

  // A global variable's address is a word address, so the variable itself
  // lives in address space 0 (the target maps the "private" language space to
  // it) and is cast to the byte-pointer space where C code takes its address.
  // The back end rejects globals in address space 1.
  LangAS getGlobalVarAddressSpace(CodeGenModule &CGM,
                                  const VarDecl *D) const override {
    if (D && D->getType().hasAddressSpace())
      return D->getType().getAddressSpace();
    return LangAS::opencl_private;
  }
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createMera400TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<Mera400TargetCodeGenInfo>(CGM.getTypes());
}
