# LLVM for the MERA-400

This repository is a fork of the [LLVM project](https://github.com/llvm/llvm-project)
that adds an **experimental backend for the MERA-400**, a 16-bit,
word-addressed Polish computer from the 1970s. With it you can compile C
and C++ for the machine using Clang, assemble with the LLVM integrated
assembler and link with LLD, instead of hand-writing assembly.

Everything else in the tree is stock LLVM; the MERA-400 work is small and
additive, so it can be rebased on upstream.

## Why a fork

The MERA-400 is unusual: memory is addressed in 16-bit *words* (not bytes),
there are only a handful of registers, and floating point is either done by
the optional AWP (floating point) unit or by library routines. These
properties need support in the compiler (`char` sizes, pointer layout, ABI),
in the assembler/object format, and in the linker (relocations in words).
Upstream LLVM has none of it, hence this fork.

## What was added

| Area | Location |
| ---- | -------- |
| Backend (instruction selection, ABI, frame lowering, AsmPrinter) | `llvm/lib/Target/Mera400/` |
| Assembler parser, disassembler, MC layer (encoding, relocations, float format) | `llvm/lib/Target/Mera400/{AsmParser,Disassembler,MCTargetDesc}/` |
| Target triple support (`mera400`, OS `none` and `crook`) | `llvm/include/llvm/TargetParser/Triple.h` |
| ELF machine id (`EM_MERA400_EXPERIMENTAL`, 0x4d34) and relocations | `llvm/include/llvm/BinaryFormat/ELF*.{h,def}`, `ELFRelocs/Mera400.def` |
| Clang target info, ABI and driver/toolchain | `clang/lib/Basic/Targets/Mera400.*`, `clang/lib/CodeGen/Targets/Mera400.cpp`, `clang/lib/Driver/ToolChains/Mera400.*` |
| Linker (LLD) support | `lld/ELF/Arch/Mera400.cpp` |
| Tests | `llvm/test/{CodeGen,MC}/Mera400`, `clang/test/*/mera400-*`, `lld/test/ELF/mera400-*` |

Objects are big-endian ELF32 files.

## Targets (triples)

| Triple | Meaning |
| ------ | ------- |
| `mera400-unknown-none` | Bare metal: no operating system. |
| `mera400-unknown-crook` | Programs for CROOK, the MERA-400 operating system. |

Useful Clang options (MERA-400 only):

* `-mcpu=generic|mera400|mx16` – CPU variant (`mx16` enables the extended instructions).
* `-mno-large-memory` (default) / `-mlarge-memory` – 16-bit or 32-bit data pointers.
* `-mawp` / `-mno-awp` – use the AWP floating point unit or call library routines.
* CROOK only: `-mcrook-version=<4|5>`, `-mcrook-base=<words>`,
  `-mcrook-image=<words>`, `-mcrook-stack=<words>`.

RTTI and exceptions are not supported.

## Building

The backend is an *experimental* LLVM target, so it is enabled through
`LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`, not `LLVM_TARGETS_TO_BUILD`.

Prerequisites: CMake (3.20+), Ninja, a C++17 compiler (GCC or Clang), Python 3
and Git.

```sh
git clone https://github.com/jplcz/mera400-llvm.git
cd mera400-llvm

cmake -G Ninja -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD="Mera400" \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DCMAKE_INSTALL_PREFIX=$HOME/mera400-toolchain

ninja -C build            # build everything
ninja -C build install    # optional: install the toolchain
```

Notes:

* Replace `X86` with your host architecture (`AArch64`, ...) or leave it out.
  It is only needed to build the host tools and run the test suite.
* `clang` and `lld` are both required: Clang compiles, LLD links. The
  assembler is built into Clang (`llvm-mc` also works).
* Add `-DLLVM_USE_LINKER=lld` or `-DLLVM_PARALLEL_LINK_JOBS=2` to speed up
  linking / reduce memory use on small machines.

### Running the tests

```sh
build/bin/llvm-lit -sv llvm/test/CodeGen/Mera400 llvm/test/MC/Mera400
build/bin/llvm-lit -sv clang/test --filter=mera400
build/bin/llvm-lit -sv lld/test --filter=mera400
```

(`ninja -C build check-llvm check-clang check-lld` runs the full suites.)

## Using the compiler

Because the machine has no operating system, Clang does not look in `/usr`.
Startup code, linker script and libraries come from a *sysroot* provided by the
[`mera400-libc`](https://github.com/jplcz/mera400-libc) C library project, laid out as:

```
<sysroot>/include/            headers
<sysroot>/lib/crt0.o          start-up code (crt0-crook.o for CROOK)
<sysroot>/lib/mera400.ld      linker script (crook.ld for CROOK)
<sysroot>/lib/libc.a          C library (libcrook.a for CROOK)
<sysroot>/lib/libbuiltins.a   integer helpers (__mulhi3, __divhi3, ...)
```

The sysroot is given with `--sysroot=<dir>`, or defaults to
`<clang directory>/../mera400`.

```sh
# Bare metal, small memory model
clang --target=mera400-unknown-none --sysroot=/path/to/sysroot -O2 hello.c -o hello.elf

# CROOK-5 program with the AWP unit
clang --target=mera400-unknown-crook --sysroot=/path/to/sysroot \
      -mcrook-version=5 -mawp -O2 hello.c -o hello.bin

# Just look at the generated assembly
clang --target=mera400-unknown-none -S -O2 foo.c -o -
```

## Debugging with LLDB (work in progress)

A debugger lets you inspect a program: see its source lines, variables and
machine code. LLDB is LLVM's debugger. So far it works *statically*, on ELF
files, and it can connect to a GDB-remote debug stub (`gdb-remote host:port`)
that describes MERA-400 registers via `lldb/test/Shell/Process/Inputs/mera400-core.xml`.
No machine or emulator provides such a stub yet; the test suite uses a small
fake one (`mera400-stub.py`) to check register, memory and unwinding support.

To build it, add `lldb` to the project list (the Python and terminal-editing
options can be switched off to keep dependencies small):

```sh
cmake -G Ninja -S llvm -B build ... \
  -DLLVM_ENABLE_PROJECTS="clang;lld;lldb" \
  -DLLDB_ENABLE_PYTHON=OFF -DLLDB_ENABLE_LUA=OFF
ninja -C build lldb
```

Compile with `-g` to get debug information. It lives only in the ELF file;
the flat image deployed to the device does not contain it, so it costs no
device memory.

```sh
build/bin/lldb hello.elf
(lldb) disassemble -n main       # machine code of main
(lldb) image lookup -a 0x100 -v  # source line and variables at an address
```

Addresses are byte addresses (twice the machine's word address).

## Upstream LLVM

For general LLVM information (other targets, contributing, community) see the
[LLVM Getting Started guide](https://llvm.org/docs/GettingStarted.html), and the
original project at <https://github.com/llvm/llvm-project>. The code is under
the Apache 2.0 license with LLVM exceptions (see `LICENSE.TXT`).
