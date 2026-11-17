// RUN: %clang -### --target=mera400-unknown-none --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mlarge-memory -mawp -mcpu=mx16 -Lextra %s 2>&1 \
// RUN:   | FileCheck %s
// RUN: %clang -### --target=mera400-unknown-none --sysroot=%S/Inputs/mera400_tree %s \
// RUN:   -nostdlib 2>&1 | FileCheck -check-prefix=NOSTDLIB %s
// RUN: not %clang --target=mera400-unknown-none -frtti -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck -check-prefix=RTTI %s
// RUN: not %clang --target=mera400-unknown-none -fexceptions -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck -check-prefix=EXC %s
// RUN: %clang -### --target=mera400-unknown-none -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck -check-prefix=DEFAULTS %s

// The sysroot's include directory is searched, and the options reach the
// compiler proper.
// CHECK: "-cc1" "-triple" "mera400-unknown-none"
// CHECK-SAME: "-target-abi" "large" "-target-feature" "+large-memory"
// CHECK-SAME: "-target-feature" "+awp" "-target-cpu" "mx16"
// CHECK-SAME: "-internal-isystem" "{{.*}}mera400_tree{{/|\\\\}}include"

// The start-up code is linked first, then the library directories, the user's
// objects and linker options, the linker script (after the user's --defsym,
// see the driver) and finally the libraries.
// CHECK: "{{.*}}ld.lld" "-m" "mera400elf" "{{.*}}mera400_tree{{/|\\\\}}lib{{/|\\\\}}crt0.o"
// CHECK-SAME: "-Lextra" "-L{{.*}}mera400_tree{{/|\\\\}}lib"
// CHECK-SAME: "-T" "{{.*}}mera400.ld"
// CHECK-SAME: "-lc" "-lbuiltins" "-o" "a.out"

// NOSTDLIB: "{{.*}}ld.lld" "-m" "mera400elf"
// NOSTDLIB-NOT: crt0.o
// NOSTDLIB-NOT: "-lc"

// RTTI: unsupported option '-frtti' for target 'mera400-unknown-none'
// EXC: unsupported option '-fexceptions' for target 'mera400-unknown-none'

// Plain char is unsigned and no C++ exit-time registration is needed.
// DEFAULTS: "-fno-signed-char" "-fno-use-cxa-atexit"
