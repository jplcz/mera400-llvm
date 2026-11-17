; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | %python %S/Inputs/float-sim.py plain | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o

; Floating point in the native 48-bit format. The IR type is `double` (the
; compiler treats float, double and long double alike); a value is kept in
; R1:R2:R3 and occupies four words in memory (three plus a zero padding word).
; Without the awp feature every operation is a call of a __m48_* runtime
; function: the first operand is in R1:R2:R3, the second one on the stack.

declare double @llvm.fabs.f64(double)
declare double @llvm.copysign.f64(double, double)
declare double @llvm.sqrt.f64(double)
declare double @llvm.powi.f64.i16(double, i16)

; SIM: float plain:

; CHECK-LABEL: add:
; CHECK:       rj r4, __m48_add
define double @add(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

; CHECK-LABEL: sub:
; CHECK:       rj r4, __m48_sub
define double @sub(double %a, double %b) {
  %r = fsub double %a, %b
  ret double %r
}

; CHECK-LABEL: mul:
; CHECK:       rj r4, __m48_mul
define double @mul(double %a, double %b) {
  %r = fmul double %a, %b
  ret double %r
}

; CHECK-LABEL: div:
; CHECK:       rj r4, __m48_div
define double @div(double %a, double %b) {
  %r = fdiv double %a, %b
  ret double %r
}

; A constant operand is converted to the native format by the compiler.
; CHECK-LABEL: addc:
; CHECK:       lw r5, 24576
; CHECK:       rj r4, __m48_add
define double @addc(double %a) {
  %r = fadd double %a, 1.5
  ret double %r
}

; CHECK-LABEL: neg:
; CHECK:       rj r4, __m48_neg
define double @neg(double %a) {
  %r = fneg double %a
  ret double %r
}

; fabs and copysign negate only when needed (via __m48_neg).
; CHECK-LABEL: abs:
; CHECK:       rj r4, __m48_neg
define double @abs(double %a) {
  %r = call double @llvm.fabs.f64(double %a)
  ret double %r
}

; CHECK-LABEL: cs:
; CHECK:       rj r4, __m48_neg
define double @cs(double %a, double %b) {
  %r = call double @llvm.copysign.f64(double %a, double %b)
  ret double %r
}

; A comparison calls __m48_cmp (-1, 0 or 1) and tests its result.
; CHECK-LABEL: maxf:
; CHECK:       rj r4, __m48_cmp
; CHECK:       cwt r1, 0
define double @maxf(double %a, double %b) {
  %c = fcmp ogt double %a, %b
  %r = select i1 %c, double %a, double %b
  ret double %r
}

; CHECK-LABEL: lt:
; CHECK:       rj r4, __m48_cmp
define i16 @lt(double %a, double %b) {
  %c = fcmp olt double %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; There is no NaN, so "ordered" is always true and "unordered" always false.
; CHECK-LABEL: ord:
; CHECK-NOT:   __m48_cmp
define i16 @ord(double %a, double %b) {
  %c = fcmp ord double %a, %b
  %u = fcmp uno double %a, %b
  %x = xor i1 %c, %u
  %r = zext i1 %x to i16
  ret i16 %r
}

; The first double goes to R1:R2:R3 and the second one to the stack; a double
; is never split between R3 and the stack.
; CHECK-LABEL: sel:
define double @sel(i1 %c, double %a, double %b) {
  %r = select i1 %c, double %a, double %b
  ret double %r
}

; CHECK-LABEL: loop:
define double @loop(double %a, double %b) {
entry:
  br label %l
l:
  %x = phi double [ %a, %entry ], [ %y, %l ]
  %i = phi i16 [ 0, %entry ], [ %n, %l ]
  %y = fadd double %x, %b
  %n = add i16 %i, 1
  %d = icmp eq i16 %n, 3
  br i1 %d, label %e, label %l
e:
  ret double %y
}

; Conversions and math functions are calls of runtime functions.
; CHECK-LABEL: conv:
; CHECK-DAG:   rj r4, __m48_toi32
; CHECK-DAG:   rj r4, __m48_tou32
; CHECK-DAG:   rj r4, __m48_toi64
; CHECK-DAG:   rj r4, __m48_tou64
; CHECK:       rj r4, __m48_fromi32
; CHECK:       rj r4, __m48_fromu32
; CHECK:       rj r4, __m48_fromi64
; CHECK:       rj r4, __m48_fromu64
define void @conv(double %a, i32 %i, i64 %l, ptr %p) {
  %a1 = fptosi double %a to i32
  %a2 = fptoui double %a to i32
  %a3 = fptosi double %a to i64
  %a4 = fptoui double %a to i64
  store volatile i32 %a1, ptr %p
  store volatile i32 %a2, ptr %p
  store volatile i64 %a3, ptr %p
  store volatile i64 %a4, ptr %p
  %b1 = sitofp i32 %i to double
  %b2 = uitofp i32 %i to double
  %b3 = sitofp i64 %l to double
  %b4 = uitofp i64 %l to double
  store volatile double %b1, ptr %p
  store volatile double %b2, ptr %p
  store volatile double %b3, ptr %p
  store volatile double %b4, ptr %p
  ret void
}

; CHECK-LABEL: math:
; CHECK:       rj r4, sqrt
; CHECK:       rj r4, __m48_powi
define double @math(double %a, i16 %n) {
  %s = call double @llvm.sqrt.f64(double %a)
  %r = call double @llvm.powi.f64.i16(double %s, i16 %n)
  ret double %r
}

; Memory access moves four words, the fourth one being zero padding.
; CHECK-LABEL: ld:
define double @ld(ptr %p) {
  %v = load double, ptr %p
  ret double %v
}

; CHECK-LABEL: st:
define void @st(ptr %p, double %v) {
  store double %v, ptr %p
  ret void
}

; A bitcast only moves the words; the value is not converted.
; CHECK-LABEL: bits:
define i64 @bits(double %a) {
  %r = bitcast double %a to i64
  ret i64 %r
}
