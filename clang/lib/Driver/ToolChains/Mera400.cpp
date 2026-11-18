//===--- Mera400.cpp - MERA-400 ToolChain Implementations -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The driver side of the MERA-400 target. The assembler is the integrated
// one and the linker is ld.lld. Because the machine has no operating system,
// nothing is looked for in /usr: the start-up file, the linker script and the
// libraries come from a C library installation (the mera400-libc project)
// laid out as
//
//     <sysroot>/include/       headers (float.h, limits.h, string.h, ...)
//     <sysroot>/lib/crt0.o     start-up code, always linked first
//     <sysroot>/lib/mera400.ld linker script
//     <sysroot>/lib/libc.a     C library
//     <sysroot>/lib/libbuiltins.a  integer helpers (__mulhi3, __divhi3, ...)
//
// The sysroot is --sysroot=<dir>, or <clang directory>/../mera400.
//
//===----------------------------------------------------------------------===//

#include "Mera400.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

Mera400ToolChain::Mera400ToolChain(const Driver &D, const llvm::Triple &Triple,
                                   const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  SmallString<128> Lib(computeSysRoot());
  llvm::sys::path::append(Lib, "lib");
  addPathIfExists(D, Lib, getFilePaths());
}

std::string Mera400ToolChain::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot;
  SmallString<128> Dir(getDriver().Dir);
  llvm::sys::path::append(Dir, "..", "mera400");
  return std::string(Dir);
}

void Mera400ToolChain::AddClangSystemIncludeArgs(
    const ArgList &DriverArgs, ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc, options::OPT_nostdlibinc))
    return;
  SmallString<128> Dir(computeSysRoot());
  llvm::sys::path::append(Dir, "include");
  addSystemInclude(DriverArgs, CC1Args, Dir.str());
}

void Mera400ToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                             ArgStringList &CC1Args,
                                             BoundArch BA,
                                             Action::OffloadKind) const {
  const Driver &D = getDriver();
  StringRef Target = getTriple().str();

  // The front end forces these off (Mera400TargetInfo::adjust); an explicit
  // request is a mistake the user should hear about instead of a silent
  // no-op, because the runtime that would support them does not exist.
  for (const Arg *A :
       DriverArgs.filtered(options::OPT_frtti, options::OPT_fexceptions,
                           options::OPT_fcxx_exceptions))
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(DriverArgs) << Target;

  // Plain char is unsigned (see Mera400TargetInfo); the driver option is only
  // passed when the user did not ask for a signed char.
  if (!DriverArgs.hasArg(options::OPT_fsigned_char,
                         options::OPT_fno_unsigned_char) &&
      !DriverArgs.hasArg(options::OPT_funsigned_char,
                         options::OPT_fno_signed_char))
    CC1Args.push_back("-fno-signed-char");
  // __cxa_atexit is part of the C++ runtime that is not provided; global
  // destructors are run by crt0 instead.
  CC1Args.push_back("-fno-use-cxa-atexit");

  // The large model needs both the ABI (it selects the data layout with 32-bit
  // byte pointers, which the front end needs to size pointers and size_t) and
  // the subtarget feature (it makes the back end use them).
  if (DriverArgs.hasFlag(options::OPT_mlarge_memory,
                         options::OPT_mno_large_memory, false)) {
    CC1Args.push_back("-target-abi");
    CC1Args.push_back("large");
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+large-memory");
  }
  // -mawp is passed in both polarities so that a later -mno-awp wins over an
  // earlier -mawp and over a CPU default.
  if (const Arg *A =
          DriverArgs.getLastArg(options::OPT_mawp, options::OPT_mno_awp)) {
    CC1Args.push_back("-target-feature");
    CC1Args.push_back(A->getOption().matches(options::OPT_mawp) ? "+awp"
                                                                : "-awp");
  }
  // The generic driver only knows -mcpu for architectures it lists by name.
  if (const Arg *A = DriverArgs.getLastArg(options::OPT_mcpu_EQ)) {
    CC1Args.push_back("-target-cpu");
    CC1Args.push_back(A->getValue());
  }
}

Tool *Mera400ToolChain::buildLinker() const {
  return new tools::mera400::Linker(*this);
}

// Finds a file of the C library: in the -L directories first (so that a user
// can override a file), then in <sysroot>/lib. An empty string if not found.
static std::string findLibraryFile(const ToolChain &TC, const ArgList &Args,
                                   StringRef Name) {
  std::vector<std::string> Dirs = Args.getAllArgValues(options::OPT_L);
  SmallString<128> Lib(
      static_cast<const Mera400ToolChain &>(TC).computeSysRoot());
  llvm::sys::path::append(Lib, "lib");
  Dirs.push_back(std::string(Lib));
  for (const std::string &Dir : Dirs) {
    SmallString<128> Path(Dir);
    llvm::sys::path::append(Path, Name);
    if (llvm::sys::fs::exists(Path))
      return std::string(Path);
  }
  return "";
}

void mera400::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  std::string Linker = TC.GetProgramPath(getShortName());
  ArgStringList CmdArgs;

  bool IsRelocatable = Args.hasArg(options::OPT_r);
  bool UseStartFiles =
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles) &&
      !IsRelocatable;
  bool UseLibs = !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                              options::OPT_nolibc) &&
                 !IsRelocatable;

  CmdArgs.push_back("-m");
  CmdArgs.push_back("mera400elf");

  Args.addAllArgs(CmdArgs, {options::OPT_s, options::OPT_t, options::OPT_u,
                            options::OPT_r});

  // The processor starts at the first word of the image, so crt0 (which
  // defines _start) must be the first object, before any user object. The
  // linker script puts its section first as well; both are done so that
  // neither alone can be broken by an unusual command line.
  if (UseStartFiles) {
    std::string Crt0 = findLibraryFile(TC, Args, "crt0.o");
    if (Crt0.empty())
      D.Diag(diag::err_drv_no_such_file)
          << "crt0.o (give the C library with --sysroot or -L, or use "
             "-nostartfiles)";
    else
      CmdArgs.push_back(Args.MakeArgString(Crt0));
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  TC.AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  // The default linker script comes after the user's linker options (-Wl,
  // --defsym=...): lld handles --defsym and the script in command line order,
  // so a symbol the script computes from one that is also set with --defsym
  // (for instance "__heap_end = __stack_top - __stack_size") sees the user's
  // value only if the --defsym came first and the script defines its own
  // default with PROVIDE().
  if (UseStartFiles && !Args.hasArg(options::OPT_T)) {
    std::string Script = findLibraryFile(TC, Args, "mera400.ld");
    if (!Script.empty()) {
      CmdArgs.push_back("-T");
      CmdArgs.push_back(Args.MakeArgString(Script));
    }
  }

  // The archives come after the objects so that the linker pulls in only the
  // functions the program uses; the order is from the most dependent (libc
  // calls the helpers) to the least dependent.
  if (UseLibs) {
    CmdArgs.push_back("-lc");
    CmdArgs.push_back("-lbuiltins");
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T);
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(), Args.MakeArgString(Linker),
      CmdArgs, Inputs, Output));
}

// ---------------------------------------------------------------------------
// CROOK. The same sysroot layout, with crt0-crook.o, crook.ld and libcrook.a
// (see the mera400-libc project). Sizes are given in words, as CROOK counts
// them; the linker script works in bytes.
// ---------------------------------------------------------------------------

// The CROOK version the program is built for: 4 or 5. Only CROOK-5 has the
// GMEM, RMEM and LRAM extracodes. Returns 0 for an invalid value.
static unsigned getCrookVersion(const ArgList &Args) {
  const Arg *A = Args.getLastArg(options::OPT_mcrook_version_EQ);
  if (!A)
    return 5;
  StringRef V = A->getValue();
  return V == "4" ? 4 : V == "5" ? 5 : 0;
}

CrookToolChain::CrookToolChain(const Driver &D, const llvm::Triple &Triple,
                               const ArgList &Args)
    : Mera400ToolChain(D, Triple, Args) {
  if (!getCrookVersion(Args))
    D.Diag(diag::err_drv_invalid_value)
        << Args.getLastArg(options::OPT_mcrook_version_EQ)->getAsString(Args)
        << Args.getLastArgValue(options::OPT_mcrook_version_EQ);
}

void CrookToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                           ArgStringList &CC1Args,
                                           BoundArch BA,
                                           Action::OffloadKind OK) const {
  Mera400ToolChain::addClangTargetOptions(DriverArgs, CC1Args, BA, OK);
  if (unsigned Version = getCrookVersion(DriverArgs))
    CC1Args.push_back(
        DriverArgs.MakeArgString("-D__CROOK_VERSION__=" + Twine(Version)));
}

Tool *CrookToolChain::buildLinker() const {
  return new tools::crook::Linker(*this);
}

// Adds --defsym=<Symbol>=<2 * words> for the option, or for the default when
// the option is absent. The value must be a plain non-negative integer.
static void addCrookDefsym(const Driver &D, const ArgList &Args,
                           ArgStringList &CmdArgs, options::ID Opt,
                           StringRef Symbol, uint64_t DefaultWords) {
  uint64_t Words = DefaultWords;
  if (const Arg *A = Args.getLastArg(Opt)) {
    unsigned Value;
    if (StringRef(A->getValue()).getAsInteger(0, Value))
      D.Diag(diag::err_drv_invalid_int_value)
          << A->getAsString(Args) << A->getValue();
    else
      Words = Value;
  }
  CmdArgs.push_back(
      Args.MakeArgString("--defsym=" + Symbol + "=" + Twine(Words * 2)));
}

void crook::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  std::string Linker = TC.GetProgramPath(getShortName());
  ArgStringList CmdArgs;

  bool IsRelocatable = Args.hasArg(options::OPT_r);
  bool UseStartFiles =
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles) &&
      !IsRelocatable;
  bool UseLibs = !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                              options::OPT_nolibc) &&
                 !IsRelocatable;

  CmdArgs.push_back("-m");
  CmdArgs.push_back("mera400elf");

  Args.addAllArgs(CmdArgs, {options::OPT_s, options::OPT_t, options::OPT_u,
                            options::OPT_r});

  // CROOK starts a program at the first word of its image: crt0-crook first.
  if (UseStartFiles) {
    std::string Crt0 = findLibraryFile(TC, Args, "crt0-crook.o");
    if (Crt0.empty())
      D.Diag(diag::err_drv_no_such_file)
          << "crt0-crook.o (give the C library with --sysroot or -L, or use "
             "-nostartfiles)";
    else
      CmdArgs.push_back(Args.MakeArgString(Crt0));
  }

  // The image parameters. They must precede -T: a plain assignment in a
  // linker script replaces an earlier --defsym, while PROVIDE() in crook.ld
  // only supplies a default when the symbol is not defined yet.
  if (!IsRelocatable) {
    addCrookDefsym(D, Args, CmdArgs, options::OPT_mcrook_base_EQ,
                   "__image_base", 0);
    addCrookDefsym(D, Args, CmdArgs, options::OPT_mcrook_image_EQ,
                   "__image_size", 8192);
    addCrookDefsym(D, Args, CmdArgs, options::OPT_mcrook_stack_EQ,
                   "__stack_size", 512);
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  TC.AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (UseStartFiles && !Args.hasArg(options::OPT_T)) {
    std::string Script = findLibraryFile(TC, Args, "crook.ld");
    if (Script.empty())
      D.Diag(diag::err_drv_no_such_file)
          << "crook.ld (give the C library with --sysroot or -L, or use "
             "-nostartfiles)";
    else {
      CmdArgs.push_back("-T");
      CmdArgs.push_back(Args.MakeArgString(Script));
    }
  }
  if (!IsRelocatable)
    CmdArgs.push_back("--gc-sections");

  if (UseLibs) {
    CmdArgs.push_back("-lcrook");
    CmdArgs.push_back("-lc");
    CmdArgs.push_back("-lbuiltins");
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T);

  // A file name ending in .bin asks for the raw memory image CROOK loads.
  StringRef OutName = Output.getFilename();
  if (!IsRelocatable && OutName.ends_with(".bin"))
    CmdArgs.push_back("--oformat=binary");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(), Args.MakeArgString(Linker),
      CmdArgs, Inputs, Output));
}
