// RUN: %clang_cc1 -triple mera400-unknown-none -target-abi large -target-feature +large-memory \
// RUN:   -fno-signed-char -fno-rtti -O0 -discard-value-names -emit-llvm %s -o - | FileCheck %s
// RUN: %clang -target mera400 -mlarge-memory -fno-signed-char -fno-rtti -O1 -S %s -o - \
// RUN:   | FileCheck --check-prefix=ASM %s
// RUN: %clang -target mera400 -mlarge-memory -fno-signed-char -fno-rtti -O1 -S %s -o - \
// RUN:   | %python %S/Inputs/mera400-large-sim.py | FileCheck --check-prefix=SIM %s

// In the large memory model Clang sizes every pointer, including pointers to
// functions, vtable entries, vptrs and member function pointers, as 4 bytes.
// The data layout therefore makes address space 1 (32-bit byte addresses) the
// program address space (P1) and the globals address space (G1), so function
// pointers and vtable slots are 32-bit too: the code and the layout agree.
// A code address is the 32-bit BYTE address of the function (the unit of an
// ELF symbol); the back end shifts it right by one to get the word address
// that "rj" needs.

struct B {
  virtual int f();
  virtual int g(int);
  int x;
};
struct D : B {
  int f() override { return 7; }
  int g(int a) override { return a * 2; }
};
int B::f() { return 1; }
int B::g(int a) { return a; }
int twice(int a) { return a + a; }
int callfp(int (*p)(int), int v) { return p(v); }
int callv(B *b, int v) { return b->g(v) + b->f(); }
int viapm(B *b, int (B::*p)(int), int v) { return (b->*p)(v); }
D d;
int (*fp)(int) = twice;
int (B::*pm)(int) = &B::g;
int ext();
struct S { int v; S() : v(ext()) {} };
S s; // dynamic initialization: a static constructor table entry

// CHECK: target datalayout = "E-m:e-P1-G1-p:16:16-p1:32:16-
// A vptr is a 4-byte pointer, the vtable has 4-byte slots (the stride the
// Itanium/ARM member function pointer code computes from the AST).
// CHECK: %struct.B = type { ptr addrspace(1), i16 }
// CHECK: @fp = global ptr addrspace(1) @_Z5twicei
// A member function pointer is {i32 function address or vtable offset, i32 adjustment}.
// CHECK: @pm = global { i32, i32 } { i32 4, i32 1 }
// CHECK: @_ZTV1B = {{.*}}addrspace(1) constant { [4 x ptr addrspace(1)] } { [4 x ptr addrspace(1)] [ptr addrspace(1) null, ptr addrspace(1) null, ptr addrspace(1) @_ZN1B1fEv, ptr addrspace(1) @_ZN1B1gEi] }
// CHECK: define {{.*}}i16 @_Z6callfpPFiiEi(ptr addrspace(1) {{.*}}, i16 {{.*}}) addrspace(1)
// CHECK: call {{.*}}addrspace(1) i16 %{{.*}}(i16

// Indirect call: byte address to word address (srz/sry), then rj.
// ASM-LABEL: _Z6callfpPFiiEi:
// ASM:        srz r1
// ASM-NEXT:   sry r2
// ASM:        rj r4, r2

// ASM:      fp:
// ASM-NEXT:   .long _Z5twicei
// ASM:      _ZTV1B:
// ASM:        .long _ZN1B1fEv
// ASM-NEXT:   .long _ZN1B1gEi
// The static constructor table holds 32-bit byte addresses as well.
// ASM:        .init_array
// ASM:        .long _GLOBAL__sub_I_

// SIM: large model calls ok (6 checks)
