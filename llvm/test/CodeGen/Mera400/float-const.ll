; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | %python %S/Inputs/float-sim.py const %s | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o

; The compiler converts IEEE constants (which is how LLVM IR spells them) to the
; native format: the mantissa is a 40-bit two's complement fraction in [0.5, 1)
; (or [-1, -0.5)), the exponent a signed byte, the result rounded to nearest.
; The simulation script has its own implementation of the conversion and
; compares it with what is returned by the functions c* and stored in the data
; of the globals g*.

; SIM: float constants:

; Return values: 1.5 = 0.75 * 2^1.
; CHECK-LABEL: c1:
; CHECK:       lw r1, 24576
; CHECK:       lwt r3, 1
define double @c1() { ret double 1.500000e+00 }
define double @c2() { ret double -2.000000e+00 }
define double @c3() { ret double 0.000000e+00 }
define double @c4() { ret double 1.000000e-01 }
define double @c5() { ret double -1.000000e-01 }
define double @c6() { ret double 1.000000e+10 }
define double @c7() { ret double -3.333333e+05 }
define double @c8() { ret double 1.000000e+38 }
define double @c9() { ret double 1.000000e-38 }
define double @c10() { ret double -5.000000e-01 }
define double @c11() { ret double 1.000000e+00 }

; Underflow is flushed to zero.
define double @c12() { ret double 1.000000e-300 }

; Rounding is to nearest, ties to even. The mantissa has 40 bits, so 1 + 2^-39
; is exactly halfway between 1 and 1 + 2^-38 (rounds to the even one, 1), and
; 1 + 3 * 2^-39 is halfway between 1 + 2^-38 and 1 + 2^-37 (rounds to the
; even one, 1 + 2^-37). The negative values mirror them.
define double @c13() { ret double 0x3FF0000000002000 }
define double @c14() { ret double 0x3FF0000000006000 }
define double @c15() { ret double 0xBFF0000000002000 }
define double @c16() { ret double 0xBFF0000000006000 }
; Just above and just below a tie.
define double @c17() { ret double 0x3FF0000000002001 }
define double @c18() { ret double 0x3FEFFFFFFFFFFFFF }

@g1 = global double 1.500000e+00
@g2 = global double -1.000000e-01
@g3 = global [3 x double] [double 1.000000e+00, double -2.500000e+00, double 3.141592653589793e+00]
@g4 = global { i16, double, i16 } { i16 7, double 1.000000e+10, i16 9 }
@g5 = constant [2 x double] [double 2.000000e+00, double 1.000000e-300]

; A constant passed to a function, merged by a phi, stored and returned.
declare double @callee(double, double)

; CHECK-LABEL: pass:
; CHECK:       rj r4, callee
define double @pass(i1 %c, ptr %p) {
entry:
  store double 2.500000e+00, ptr %p
  br i1 %c, label %a, label %b
a:
  br label %m
b:
  br label %m
m:
  %x = phi double [ 1.250000e+00, %a ], [ -7.500000e-01, %b ]
  %r = call double @callee(double %x, double 6.250000e-02)
  ret double %r
}

; The data is written as exact words (not with the .float directive, which
; drops the extra mantissa bits where the compiler rounds): -0.1 is
; 0x999999999a (not ...99) with the exponent -3 (0xfd).
; CHECK-LABEL: g2:
; CHECK-NEXT:  .quad {{.*}} ; 0x999999999afd0000
