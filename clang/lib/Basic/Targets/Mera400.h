//===--- Mera400.h - Declare MERA-400 target feature support ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MERA-400 TargetInfo: the sizes and alignments of the
// C types, the predefined macros and the target features (AWP, MX-16, the
// large memory model). See docs/design/mera400-llvm.md, "Slice: code
// generation 7", for the reasoning behind each choice.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_MERA400_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_MERA400_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

// The MERA-400 is word addressed but C needs byte addresses, so a C data
// pointer (the default language address space) is a byte pointer in LLVM
// address space 1 (16 bits in the small model, 32 in the large one). The back
// end keeps address space 0 for word addresses: stack slots, globals and
// functions. Clang's private space is mapped to 0 so that code generation can
// ask for "private" storage and cast it to the default space.
static constexpr LangASMap Mera400AddrSpaceMap = {
    {LangAS::Default, 1},
    {LangAS::opencl_private, 0},
};

class LLVM_LIBRARY_VISIBILITY Mera400TargetInfo : public TargetInfo {
  bool HasAWP = false;
  bool HasMX16 = false;
  bool Large = false;
  std::string ModelName = "small";
  std::string CPU = "generic";

  // Clang cannot give a data pointer and a function pointer different widths,
  // so the large model widens both; see setABI.
  void configureModel(bool IsLarge);

public:
  Mera400TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  /// Replaces the generic IEEE based __FLT_*, __DBL_* and __LDBL_* macros.
  void defineNativeFloatMacros(MacroBuilder &Builder) const;
  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  // String literals and other constants are globals, which live in address
  // space 0 like every other global (see Mera400TargetCodeGenInfo).
  std::optional<LangAS> getConstantAddressSpace() const override {
    return LangAS::opencl_private;
  }

  // Forces the C++ options this target cannot support: see the definition.
  void adjust(DiagnosticsEngine &Diags, LangOptions &Opts,
              const TargetInfo *Aux) override;

  // The ABI name is the memory model ("small" or "large"); the LLVM target
  // parser derives the data layout from it.
  StringRef getABI() const override { return ModelName; }
  bool setABI(const std::string &Name) override;

  bool setCPU(StringRef Name) override;
  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                      StringRef CPU,
                      const std::vector<std::string> &FeaturesVec) const override;
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;
  bool hasFeature(StringRef Feature) const override;

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;

  std::string_view getClobbers() const override { return ""; }

  // A va_list is a byte pointer to the next argument slot (char * style).
  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }
};

} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_MERA400_H
