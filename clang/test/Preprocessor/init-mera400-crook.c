// RUN: %clang_cc1 -E -dM -ffreestanding -fno-signed-char -triple=mera400-unknown-crook \
// RUN:   -x c /dev/null | FileCheck -match-full-lines -check-prefix=CROOK %s
// RUN: %clang_cc1 -E -dM -ffreestanding -fno-signed-char -triple=mera400-unknown-none \
// RUN:   -x c /dev/null | FileCheck -check-prefix=NONE %s
// RUN: %clang -E -dM --target=mera400-unknown-crook -x c /dev/null \
// RUN:   | FileCheck -check-prefix=V5 %s
// RUN: %clang -E -dM --target=mera400-unknown-crook -mcrook-version=5 -x c /dev/null \
// RUN:   | FileCheck -check-prefix=V5 %s
// RUN: %clang -E -dM --target=mera400-unknown-crook -mcrook-version=4 -x c /dev/null \
// RUN:   | FileCheck -check-prefix=V4 %s
// RUN: %clang -E -dM --target=mera400-unknown-none -x c /dev/null \
// RUN:   | FileCheck -check-prefix=NONE %s

// The driver defines the CROOK version (4 or 5; 5 by default).
// V5: #define __CROOK_VERSION__ 5
// V4: #define __CROOK_VERSION__ 4

// CROOK is the MERA-400 operating system: the bare target's macros plus the OS.
// CROOK: #define __CHAR_UNSIGNED__ 1
// CROOK: #define __CROOK__ 1
// CROOK: #define __MERA400__ 1
// CROOK: #define __POINTER_WIDTH__ 16
// CROOK: #define __crook__ 1
// CROOK: #define __mera400__ 1

// NONE-NOT: __CROOK_VERSION__
// NONE-NOT: __CROOK__
// NONE-NOT: __crook__
// NONE: #define __MERA400__ 1
