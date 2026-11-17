// RUN: %clang_cc1 -triple mera400-unknown-none -O2 -emit-llvm %s -o - | FileCheck %s

// A double built from words through a union must be read from memory: the
// optimizer must not fold the words into an IEEE bit pattern.
double f() {
  union { double d; unsigned short w[4]; } r;
  r.w[0] = 1; r.w[1] = 2; r.w[2] = 3; r.w[3] = 0;
  return r.d;
}
// CHECK-LABEL: @f(
// CHECK-NOT: ret double 0x
// CHECK: load volatile double
