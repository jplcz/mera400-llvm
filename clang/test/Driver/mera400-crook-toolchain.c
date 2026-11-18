// RUN: %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   -Lextra %s -o %t.elf 2>&1 | FileCheck %s
// RUN: %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mcrook-base=2048 -mcrook-image=0x4000 -mcrook-stack=256 %s -o %t.bin 2>&1 \
// RUN:   | FileCheck -check-prefix=OPTS %s
// RUN: %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   -nostdlib %s 2>&1 | FileCheck -check-prefix=NOSTDLIB %s
// RUN: not %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mcrook-image=lots %s 2>&1 | FileCheck -check-prefix=BAD %s
// RUN: not %clang -### --target=mera400-unknown-none --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mcrook-base=2 %s 2>&1 | FileCheck -check-prefix=NONE %s
// RUN: %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   %s 2>&1 | FileCheck -check-prefix=VER5 %s
// RUN: %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mcrook-version=4 %s 2>&1 | FileCheck -check-prefix=VER4 %s
// RUN: %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mcrook-version=4 -mcrook-version=5 %s 2>&1 | FileCheck -check-prefix=VER5 %s
// RUN: not %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mcrook-version=3 %s 2>&1 | FileCheck -check-prefix=VERBAD %s
// RUN: not %clang -### --target=mera400-unknown-crook --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mcrook-version=4x %s 2>&1 | FileCheck -check-prefix=VERBAD4X %s
// RUN: not %clang -### --target=mera400-unknown-none --sysroot=%S/Inputs/mera400_tree \
// RUN:   -mcrook-version=4 %s 2>&1 | FileCheck -check-prefix=VERNONE %s

// The CROOK version (4 or 5, default 5) becomes the __CROOK_VERSION__ macro.
// VER5: "-cc1" {{.*}}"-D__CROOK_VERSION__=5"
// VER4: "-cc1" {{.*}}"-D__CROOK_VERSION__=4"
// VERBAD: invalid value '3' in '-mcrook-version=3'
// VERBAD4X: invalid value '4x' in '-mcrook-version=4x'
// VERNONE: unsupported option '-mcrook-version=' for target 'mera400-unknown-none'

// The CROOK toolchain: crt0-crook.o first, then the user's objects, the
// image parameters in bytes (twice the words; the defaults are 0, 8192 and
// 512 words) BEFORE the linker script, --gc-sections, then libcrook.a, libc.a
// and libbuiltins.a. An output not ending in .bin is an ELF file.
// CHECK: "-cc1" "-triple" "mera400-unknown-crook"
// CHECK: "{{.*}}ld.lld" "-m" "mera400elf" "{{.*}}mera400_tree{{/|\\\\}}lib{{/|\\\\}}crt0-crook.o"
// CHECK-SAME: "--defsym=__image_base=0" "--defsym=__image_size=16384" "--defsym=__stack_size=1024"
// CHECK-SAME: "-Lextra" "-L{{.*}}mera400_tree{{/|\\\\}}lib"
// CHECK-SAME: "-T" "{{.*}}crook.ld" "--gc-sections"
// CHECK-SAME: "-lcrook" "-lc" "-lbuiltins"
// CHECK-NOT: --oformat
// CHECK-SAME: "-o" "{{.*}}.elf"

// The options are in words; an output named *.bin is a raw image.
// OPTS: "--defsym=__image_base=4096" "--defsym=__image_size=32768" "--defsym=__stack_size=512"
// OPTS-SAME: "-T"
// OPTS-SAME: "--oformat=binary" "-o" "{{.*}}.bin"

// NOSTDLIB: "{{.*}}ld.lld" "-m" "mera400elf"
// NOSTDLIB-NOT: crt0-crook.o
// NOSTDLIB-NOT: "-T"
// NOSTDLIB-NOT: "-lc"

// BAD: invalid integral value 'lots' in '-mcrook-image=lots'

// On the bare target the options are rejected like other target options.
// NONE: unsupported option '-mcrook-base=' for target 'mera400-unknown-none'
