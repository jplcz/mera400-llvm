// RUN: %clang_cc1 -triple mera400-unknown-none -fno-signed-char -O1 -emit-llvm %s -o %t.none.ll
// RUN: %clang_cc1 -triple mera400-unknown-crook -fno-signed-char -O1 -emit-llvm %s -o %t.crook.ll
// RUN: grep -v '^target triple' %t.none.ll > %t.none.body
// RUN: grep -v '^target triple' %t.crook.ll > %t.crook.body
// RUN: diff %t.none.body %t.crook.body
// RUN: FileCheck -check-prefix=TRIPLE %s < %t.crook.ll
// RUN: %clang_cc1 -triple mera400-unknown-none -fno-signed-char -O1 -S %s -o %t.none.s
// RUN: %clang_cc1 -triple mera400-unknown-crook -fno-signed-char -O1 -S %s -o %t.crook.s
// RUN: diff %t.none.s %t.crook.s
// RUN: FileCheck -check-prefix=ASM %s < %t.crook.s

// CROOK changes nothing in code generation: the data layout, the ABI and the
// assembly are those of the bare target; only the module's triple differs.
// TRIPLE: target datalayout = "E-m:e-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:0:16-n16-S16"
// TRIPLE: target triple = "mera400-unknown-crook"
// ASM: sum:
// ASM: lw
struct S { int a; long b; };

int sum(struct S *p, int n) {
  int t = 0;
  for (int i = 0; i < n; ++i)
    t += p[i].a + (int)p[i].b;
  return t;
}
