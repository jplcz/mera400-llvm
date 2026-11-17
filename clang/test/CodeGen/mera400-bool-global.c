// RUN: %clang_cc1 -triple mera400-unknown-none -target-abi large -O2 -S -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mera400-unknown-none -target-abi large -O0 -S -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mera400-unknown-none -O2 -S -o - %s | FileCheck %s --check-prefix=SMALL

// A byte sized global must be stored and loaded at the same place. "rb" takes
// a byte address, so storing with "rb r1, f" used the word address of f as a
// byte address while the load read the word at f (large model).
static _Bool f;
void set(void) { f = 1; }
_Bool get(void) { return f; }

char c[4];
void setc(void) { c[1] = 7; }

// CHECK-LABEL: set:
// CHECK-NOT:   rb
// CHECK:       rw r1, f
// CHECK-LABEL: get:
// CHECK:       tw r1, f
// CHECK-LABEL: setc:
// CHECK-NOT:   rb
// CHECK:       rw r1, c

// SMALL-LABEL: set:
// SMALL-NOT:   rb r1, f
