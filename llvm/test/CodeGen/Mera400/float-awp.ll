; RUN: llc -mtriple=mera400-unknown-none -mattr=+awp -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -mattr=+awp < %s | %python %S/Inputs/float-sim.py awp | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -mattr=+awp < %s | llvm-mc -triple=mera400-unknown-none -mattr=+awp -filetype=obj -o %t.o
; RUN: llc -mtriple=mera400-unknown-none -mattr=+awp < %s | FileCheck --check-prefix=NOCALL %s

; With the awp feature (the arithmetic processor, AWP) the four operations are
; single instructions: af, sf, mf and df (add, subtract, multiply, divide
; floating point). They work on the accumulator R1:R2:R3 and read the second
; operand from memory, so the compiler stores it in a temporary on the stack.
; Everything else (negation, comparison, conversions) still uses the runtime
; functions, as the processor has no such instruction.

; SIM: float awp:

; NOCALL-NOT: __m48_add
; NOCALL-NOT: __m48_sub
; NOCALL-NOT: __m48_mul
; NOCALL-NOT: __m48_div

; CHECK-LABEL: add:
; CHECK:       af {{[0-9]+}}+r7
define double @add(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

; CHECK-LABEL: sub:
; CHECK:       sf {{[0-9]+}}+r7
define double @sub(double %a, double %b) {
  %r = fsub double %a, %b
  ret double %r
}

; CHECK-LABEL: mul:
; CHECK:       mf {{[0-9]+}}+r7
define double @mul(double %a, double %b) {
  %r = fmul double %a, %b
  ret double %r
}

; CHECK-LABEL: div:
; CHECK:       df {{[0-9]+}}+r7
define double @div(double %a, double %b) {
  %r = fdiv double %a, %b
  ret double %r
}

; CHECK-LABEL: addc:
; CHECK:       af {{([0-9]+\+)?}}r7
define double @addc(double %a) {
  %r = fadd double %a, 1.5
  ret double %r
}

; CHECK-LABEL: loop:
; CHECK:       af {{([0-9]+\+)?}}r7
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

; Negation still calls the runtime library.
; CHECK-LABEL: neg:
; CHECK:       rj r4, __m48_neg
define double @neg(double %a) {
  %r = fneg double %a
  ret double %r
}
