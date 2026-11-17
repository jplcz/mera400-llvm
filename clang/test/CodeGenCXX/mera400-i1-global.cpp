// RUN: %clang_cc1 -triple mera400-unknown-none -O2 -emit-obj -o /dev/null %s
// RUN: %clang_cc1 -triple mera400-unknown-none -target-abi large -O2 -emit-obj -o /dev/null %s
// RUN: %clang_cc1 -triple mera400-unknown-none -O2 -S -o - %s | FileCheck %s

// At -O2 GlobalOpt turns the field of a global that only ever holds two
// constants into an i1 global; the back end used to reject loads and stores
// of an i1 in memory.
struct L {
  int c;
  L(int x) : c(x) {}
};
static L a(0);
int flag;
void set() { if (flag) a.c = 'A'; }
// CHECK-LABEL: _Z3getv:
// CHECK: lb
int get() { return a.c; }
