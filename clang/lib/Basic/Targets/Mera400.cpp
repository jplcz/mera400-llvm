//===--- Mera400.cpp - Implement MERA-400 target feature support ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MERA-400 TargetInfo. The numbers come from the
// design document (docs/design/mera400-llvm.md, slice 7): a 16-bit word
// machine, big-endian, byte-addressed C pointers, one floating point format.
//
//===----------------------------------------------------------------------===//

#include "Mera400.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

Mera400TargetInfo::Mera400TargetInfo(const llvm::Triple &Triple,
                                     const TargetOptions &Opts)
    : TargetInfo(Triple) {
  BigEndian = true;
  TLSSupported = false;
  // The load instruction (lb) zero-extends a byte, so an unsigned char needs
  // no sign extension step. Plain char signedness is a language option, set
  // by the driver (-fno-signed-char, see Mera400ToolChain).
  ShortWidth = ShortAlign = 16;
  IntWidth = IntAlign = 16;
  LongWidth = 32;
  LongLongWidth = 64;
  // The machine word is 16 bits: nothing is ever aligned more strictly.
  LongAlign = LongLongAlign = 16;
  SuitableAlign = 16;
  DefaultAlignForAttributeAligned = 16;

  // float, double and long double are one format, the native 48-bit one;
  // LLVM and Clang model it as an IEEE double (the back end converts).
  FloatWidth = DoubleWidth = LongDoubleWidth = 64;
  FloatAlign = DoubleAlign = LongDoubleAlign = 16;
  FloatFormat = DoubleFormat = LongDoubleFormat = &llvm::APFloat::IEEEdouble();

  IntMaxType = SignedLongLong;
  Int64Type = SignedLongLong;
  WCharType = UnsignedShort;
  WIntType = UnsignedInt;
  Char16Type = UnsignedShort;
  Char32Type = UnsignedLong;
  SigAtomicType = SignedInt;

  // No atomic instructions exist.
  MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 0;

  // The Itanium C++ ABI with ARM-style member function pointers: the "virtual"
  // flag lives in the adjustment word, because a function word address uses
  // all 16 bits of the pointer, so its low bit cannot carry the flag.
  TheCXXABI.set(TargetCXXABI::GenericARM);

  AddrSpaceMap = &Mera400AddrSpaceMap;

  configureModel(Opts.ABI == "large");
  for (const std::string &F : Opts.FeaturesAsWritten)
    if (F == "+large-memory")
      configureModel(true);
}

void Mera400TargetInfo::configureModel(bool IsLarge) {
  Large = IsLarge;
  PointerWidth = PointerAlign = 16;
  if (Large) {
    PointerWidth = 32;
    SizeType = UnsignedLong;
    PtrDiffType = IntPtrType = SignedLong;
  } else {
    SizeType = UnsignedInt;
    PtrDiffType = IntPtrType = SignedInt;
  }
  // The data layout is computed by the LLVM target parser, so that Clang and
  // the back end cannot disagree about it.
  ModelName = Large ? "large" : "small";
  resetDataLayout();
}

void Mera400TargetInfo::adjust(DiagnosticsEngine &Diags, LangOptions &Opts,
                               const TargetInfo *Aux) {
  TargetInfo::adjust(Diags, Opts, Aux);
  // Exceptions and RTTI (run-time type information) need an unwinder and
  // type-info runtime that do not exist yet, so they are off whatever the
  // front end defaults are; the driver rejects an explicit request for them.
  // Thread-safe statics would call the __cxa_guard_* functions, which the C
  // library does not have, and a program has only one thread anyway.
  Opts.RTTI = Opts.RTTIData = false;
  Opts.Exceptions = Opts.CXXExceptions = false;
  Opts.ThreadsafeStatics = false;
}

bool Mera400TargetInfo::setABI(const std::string &Name) {
  if (Name != "small" && Name != "large")
    return false;
  configureModel(Name == "large");
  return true;
}

bool Mera400TargetInfo::setCPU(StringRef Name) {
  if (!isValidCPUName(Name))
    return false;
  CPU = Name.str();
  HasMX16 = Name == "mx16";
  return true;
}

bool Mera400TargetInfo::isValidCPUName(StringRef Name) const {
  return Name == "generic" || Name == "mera400" || Name == "mx16";
}

void Mera400TargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  Values.push_back("generic");
  Values.push_back("mera400");
  Values.push_back("mx16");
}

bool Mera400TargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  if (CPU == "mx16")
    Features["mx16"] = true;
  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
}

bool Mera400TargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                             DiagnosticsEngine &Diags) {
  for (const std::string &F : Features) {
    if (F == "+awp")
      HasAWP = true;
    else if (F == "-awp")
      HasAWP = false;
    else if (F == "+mx16")
      HasMX16 = true;
    else if (F == "-mx16")
      HasMX16 = false;
    else if (F == "+large-memory")
      configureModel(true);
    else if (F == "-large-memory")
      configureModel(false);
  }
  return true;
}

bool Mera400TargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("mera400", true)
      .Case("awp", HasAWP)
      .Case("mx16", HasMX16)
      .Case("large-memory", Large)
      .Default(false);
}

void Mera400TargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  Builder.defineMacro("__mera400__");
  Builder.defineMacro("__MERA400__");
  if (getTriple().isOSCROOK()) {
    Builder.defineMacro("__crook__");
    Builder.defineMacro("__CROOK__");
  }
  // The generic __BYTE_ORDER__ is defined by Clang itself; this is the
  // spelling some portable code tests for.
  Builder.defineMacro("__BIG_ENDIAN__");
  if (Large)
    Builder.defineMacro("__MERA400_LARGE__");
  if (HasAWP)
    Builder.defineMacro("__MERA400_AWP__");
  if (HasMX16)
    Builder.defineMacro("__MERA400_MX16__");
  // __STDC_IEC_559__ is deliberately left undefined: the floating point
  // format is not IEEE (no infinity, NaN or denormals).
  defineNativeFloatMacros(Builder);
}

// The generic code derives the __FLT_*, __DBL_* and __LDBL_* macros from the
// llvm::fltSemantics returned by getFloatFormat(), and the only semantics it
// can give us are IEEE ones (float, double and long double are all the IEEE
// double stand-in, see the constructor). The machine's real format (the one
// the AWP arithmetic unit works on) is a 39-bit magnitude plus sign mantissa
// in [1/2, 1) and an 8-bit exponent, with no infinity, NaN or denormals.
// Rather than add a custom semantics to the generic APFloat code, replace the
// macros here: the target defines are emitted after the generic ones, so a
// #undef and #define pair wins. The values are those of the C library's
// <float.h> (a mantissa of 39 bits, so 2^-38 epsilon; the largest value is
// 2^127, the smallest normalized one 2^-129).
void Mera400TargetInfo::defineNativeFloatMacros(MacroBuilder &Builder) const {
  struct Format {
    const char *Prefix; // __FLT_, __DBL_ and __LDBL_ (without the underscores).
    const char *Suffix; // Literal suffix: F (float), none, L (long double).
  };
  static const Format Formats[] = {{"FLT", "F"}, {"DBL", ""}, {"LDBL", "L"}};
  struct Macro {
    const char *Name;
    const char *Value;
    bool IsLiteral; // The value is a floating point literal: add the suffix.
  };
  static const Macro Macros[] = {
      {"MANT_DIG", "39", false},
      {"DIG", "11", false},
      // Digits needed to print a value and read it back: ceil(1 + 39 log10 2).
      {"DECIMAL_DIG", "13", false},
      {"MIN_EXP", "(-128)", false},
      {"MAX_EXP", "127", false},
      {"MIN_10_EXP", "(-38)", false},
      {"MAX_10_EXP", "38", false},
      {"MAX", "1.70141183e+38", true},
      {"NORM_MAX", "1.70141183e+38", true},
      {"MIN", "1.46936794e-39", true},
      // No denormals: the smallest positive value is the smallest normalized one.
      {"DENORM_MIN", "1.46936794e-39", true},
      {"EPSILON", "3.63797881e-12", true},
      {"HAS_DENORM", "0", false},
      {"HAS_INFINITY", "0", false},
      {"HAS_QUIET_NAN", "0", false},
  };
  for (const Format &F : Formats) {
    for (const Macro &M : Macros) {
      std::string Name = (llvm::Twine("__") + F.Prefix + "_" + M.Name + "__").str();
      Builder.undefineMacro(Name);
      Builder.defineMacro(Name, M.IsLiteral ? std::string(M.Value) + F.Suffix
                                            : std::string(M.Value));
    }
  }
  Builder.undefineMacro("__DECIMAL_DIG__");
  Builder.defineMacro("__DECIMAL_DIG__", "13");
}

ArrayRef<const char *> Mera400TargetInfo::getGCCRegNames() const {
  static const char *const GCCRegNames[] = {"r0", "r1", "r2", "r3",
                                            "r4", "r5", "r6", "r7"};
  return llvm::ArrayRef(GCCRegNames);
}

bool Mera400TargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  case 'r':
    Info.setAllowsRegister();
    return true;
  default:
    return false;
  }
}
