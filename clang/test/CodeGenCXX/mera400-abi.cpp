// RUN: %clang_cc1 -triple mera400-unknown-none -fno-signed-char -O0 -discard-value-names -emit-llvm %s -o - \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple mera400-unknown-none -fno-signed-char -fdump-record-layouts \
// RUN:   -emit-llvm-only %s | FileCheck -check-prefix=LAYOUT %s

// The C++ ABI is the ARM variant of the Itanium one: constructors return
// "this", and a member function pointer is a {pointer, adjustment} pair with
// the virtual bit in the pointer.
struct B {
  virtual int f();
  int x;
  B(int v) : x(v) {}
  int g() const { return x; }
};

B gb(1);
int B::f() { return 1; }
int call(B &b) { return b.f(); }

// A non-virtual member function pointer holds the function's word address.
int (B::*pm)() const = &B::g;

// CHECK: @pm = {{.*}}global { i16, i16 } { i16 ptrtoint (ptr @_ZNK1B1gEv to i16), i16 0 }
// CHECK: @_ZTV1B = {{.*}}constant { [3 x ptr] }
// CHECK: define {{.*}}ptr addrspace(1) @_ZN1BC1Ei(ptr addrspace(1) noundef returned
// CHECK-LABEL: define {{.*}}@_Z4callR1B

// LAYOUT: *** Dumping AST Record Layout
// LAYOUT: 0 | struct B
// LAYOUT: 0 |   (B vtable pointer)
// LAYOUT: 2 |   int x
// LAYOUT: | [sizeof=4, dsize=4, align=2,
