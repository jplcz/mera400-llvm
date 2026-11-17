// RUN: %clang_cc1 -triple mera400-unknown-none -fno-signed-char -O1 -discard-value-names -emit-llvm %s -o - \
// RUN:   | FileCheck %s

// A variable argument list is a byte pointer to the next 2-byte slot. An int
// takes one slot, a long two and a double three (the padding word of its
// four-word memory image is not passed). The optimiser merges the pointer
// steps, so the offsets from the start are checked: 2, 2 + 4 and 6 + 6.
typedef __builtin_va_list va_list;
#define va_start(ap, p) __builtin_va_start(ap, p)
#define va_arg(ap, t) __builtin_va_arg(ap, t)
#define va_end(ap) __builtin_va_end(ap)

// CHECK-LABEL: define {{.*}}i16 @va(i16 noundef %0, ...)
// CHECK: call void @llvm.va_start.p1(ptr addrspace(1)
// CHECK: getelementptr inbounds{{.*}} i8, ptr addrspace(1) %{{.*}}, i16 2
// CHECK: getelementptr inbounds{{.*}} i8, ptr addrspace(1) %{{.*}}, i16 6
// CHECK: getelementptr inbounds{{.*}} i8, ptr addrspace(1) %{{.*}}, i16 12
// CHECK: call void @llvm.va_end.p1(ptr addrspace(1)
int va(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int s = va_arg(ap, int);
  long l = va_arg(ap, long);
  double q = va_arg(ap, double);
  va_end(ap);
  return s + (int)l + (int)q;
}
