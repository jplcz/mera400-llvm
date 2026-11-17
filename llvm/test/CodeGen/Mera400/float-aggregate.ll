; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -mattr=+awp < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o

; Floating point constants inside structures and arrays, and constants that
; travel through insertvalue, extractvalue, freeze and phi, are converted to
; the native format just like plain ones. In the native format 1.5 is the
; words 24576, 0, 1 (the mantissa 0.75 and the exponent 1), -2.0 is
; -32768, 0, 1, 0.5 is 16384, 0, 0 and 3.0 is 24576, 0, 2. The IEEE words of
; the same numbers (16376 for 1.5, -16384 for -2.0, 16352 for 0.5 and 16392
; for 3.0) must never appear.

declare void @use({ double, double })
declare void @use_array([2 x double])
declare void @mixed(i16, { i16, double })

; CHECK-NOT: {{ (16376|-16384|16352|16392)$}}

; A structure that is returned through memory (it is too big for R1..R3).
; CHECK-LABEL: ret_struct:
; CHECK:       lw r{{[0-9]}}, -32768
; CHECK:       lw r{{[0-9]}}, 24576
define { double, double } @ret_struct() {
  ret { double, double } { double 1.5, double -2.0 }
}

; CHECK-LABEL: ret_array:
; CHECK:       lw r{{[0-9]}}, 24576
define [2 x double] @ret_array() {
  ret [2 x double] [double 1.5, double -2.0]
}

; A structure that fits in R1..R3 is returned in registers.
; CHECK-LABEL: ret_reg:
; CHECK:       lw r1, 24576
; CHECK:       lwt r3, 1
define { double } @ret_reg() {
  ret { double } { double 1.5 }
}

; The constant is put in with insertvalue and taken out with extractvalue.
; CHECK-LABEL: insert_extract:
; CHECK:       lw r1, 24576
; CHECK:       lwt r3, 2
define double @insert_extract() {
  %a = insertvalue { double, double } { double 1.5, double 2.0 }, double 3.0, 1
  %e = extractvalue { double, double } %a, 1
  ret double %e
}

; CHECK-LABEL: insert_ret:
; CHECK:       lw r{{[0-9]}}, 24576
define { double, double } @insert_ret(double %x) {
  %a = insertvalue { double, double } undef, double 1.5, 0
  %b = insertvalue { double, double } %a, double %x, 1
  ret { double, double } %b
}

; The same aggregate is used by a computation as well, which needs the
; constant as a value.
; CHECK-LABEL: insert_add:
; CHECK:       lw r{{[0-9]}}, 24576
; CHECK:       {{rj r4, __m48_add|af }}
define double @insert_add(double %y) {
  %a = insertvalue { double, double } { double 1.5, double 2.0 }, double %y, 1
  %e = extractvalue { double, double } %a, 0
  %r = fadd double %e, 1.0
  ret double %r
}

; CHECK-LABEL: call_struct:
; CHECK:       lw r{{[0-9]}}, 24576
; CHECK:       rj r4, use
define void @call_struct() {
  call void @use({ double, double } { double 1.5, double -2.0 })
  ret void
}

; CHECK-LABEL: call_array:
; CHECK:       lw r{{[0-9]}}, 24576
; CHECK:       rj r4, use_array
define void @call_array() {
  call void @use_array([2 x double] [double 1.5, double -2.0])
  ret void
}

; CHECK-LABEL: call_mixed:
; CHECK:       lw r{{[0-9]}}, 24576
; CHECK:       rj r4, mixed
define void @call_mixed() {
  call void @mixed(i16 1, { i16, double } { i16 2, double 1.5 })
  ret void
}

; CHECK-LABEL: phi_struct:
; CHECK:       lw r{{[0-9]}}, 24576
; CHECK:       lw r{{[0-9]}}, 16384
define { double, double } @phi_struct(i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %m
b:
  br label %m
m:
  %x = phi { double, double } [ { double 1.5, double 2.0 }, %a ], [ { double 0.5, double 4.0 }, %b ]
  ret { double, double } %x
}

; CHECK-LABEL: freeze_const:
; CHECK:       lw r1, 24576
; CHECK:       lwt r3, 1
define double @freeze_const() {
  %f = freeze double 1.5
  ret double %f
}

; CHECK-LABEL: select_struct:
; CHECK:       lw r{{[0-9]}}, -32768
define { double, double } @select_struct(i1 %c, { double, double } %x) {
  %r = select i1 %c, { double, double } { double 1.5, double -2.0 }, { double, double } %x
  ret { double, double } %r
}

; CHECK-LABEL: nested:
; CHECK:       lw r{{[0-9]}}, 16384
; CHECK:       lw r{{[0-9]}}, 24576
define [2 x [2 x double]] @nested() {
  ret [2 x [2 x double]] [[2 x double] [double 1.5, double 0.5], [2 x double] [double -2.0, double 3.0]]
}

; Negative zero and values that underflow have no native counterpart and
; become zero.
; CHECK-LABEL: zeros:
; CHECK:       lwt r1, 0
; CHECK:       lwt r2, 0
; CHECK:       lwt r3, 0
; CHECK:       rj r4, use_array
define void @zeros() {
  call void @use_array([2 x double] [double -0.0, double 1.0e-300])
  ret void
}
