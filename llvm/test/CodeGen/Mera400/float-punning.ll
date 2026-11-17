; RUN: opt -mtriple=mera400-unknown-none -passes=mera400-punned-floats -S %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -O2 %s -o - | FileCheck %s --check-prefix=ASM

; Words stored to an object that is also loaded as a double (a C union) must
; stay real memory accesses: the optimizer would otherwise fold them into an
; IEEE bit pattern, but the memory format of a double is the native one.

; CHECK-LABEL: @punned(
; CHECK: store volatile i16 1
; CHECK: store volatile i16 0
; CHECK: load volatile double
; ASM-LABEL: punned:
define double @punned() {
  %r = alloca double, align 2
  store i16 1, ptr %r, align 2
  %p = getelementptr i8, ptr %r, i16 6
  store i16 0, ptr %p, align 2
  %d = load double, ptr %r, align 2
  ret double %d
}

; An object that is only used as a double is left alone.
; CHECK-LABEL: @plain(
; CHECK-NOT: volatile
define double @plain(double %x) {
  %r = alloca double, align 2
  store double %x, ptr %r, align 2
  %d = load double, ptr %r, align 2
  ret double %d
}

; A global whose initializer words are read as a double is punned as well.
@g = global [4 x i16] [i16 1, i16 2, i16 3, i16 0], align 2
; CHECK-LABEL: @fromglobal(
; CHECK: load volatile double
define double @fromglobal() {
  %d = load double, ptr @g, align 2
  ret double %d
}
