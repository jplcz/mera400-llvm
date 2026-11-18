//===--- Mera400.h - MERA-400 ToolChain Implementations ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MERA400_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MERA400_H

#include "Gnu.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace toolchains {

/// The MERA-400 is a bare-metal target: there is no host operating system, so
/// no standard include directories and libraries are assumed. They come from
/// a C library installation (the mera400-libc project) given by --sysroot,
/// and the -I and -L options.
class LLVM_LIBRARY_VISIBILITY Mera400ToolChain : public Generic_ELF {
public:
  Mera400ToolChain(const Driver &D, const llvm::Triple &Triple,
                   const llvm::opt::ArgList &Args);

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args, BoundArch BA,
                             Action::OffloadKind) const override;

  // Absolute addresses are used; there is no dynamic loader.
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return true; }

  UnwindLibType
  GetUnwindLibType(const llvm::opt::ArgList &Args) const override {
    return UNW_None;
  }

  /// The directory with include/ and lib/ of the C library.
  std::string computeSysRoot() const override;

protected:
  Tool *buildLinker() const override;
};

/// The MERA-400 under CROOK, the machine's operating system (mera400-unknown-
/// crook). Code generation is the same as for the bare target; only linking
/// differs: the sysroot's lib/ holds crt0-crook.o, crook.ld, libcrook.a, libc.a
/// and libbuiltins.a, and the result is a raw memory image for CROOK.
class LLVM_LIBRARY_VISIBILITY CrookToolChain : public Mera400ToolChain {
public:
  CrookToolChain(const Driver &D, const llvm::Triple &Triple,
                 const llvm::opt::ArgList &Args)
      : Mera400ToolChain(D, Triple, Args) {}

protected:
  Tool *buildLinker() const override;
};

} // end namespace toolchains

namespace tools {
namespace crook {

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("Crook::Linker", "ld.lld", TC) {}
  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // end namespace crook

namespace mera400 {

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("Mera400::Linker", "ld.lld", TC) {}
  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // end namespace mera400
} // end namespace tools
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MERA400_H
