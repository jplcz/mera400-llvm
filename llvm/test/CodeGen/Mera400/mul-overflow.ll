; RUN: llc -mtriple=mera400-unknown-none -O2 < %s | FileCheck %s

; A 16-bit multiplication with overflow calls the 32-bit multiplication with
; two whole 32-bit arguments: the first in R1:R2, the second (which would be
; torn between R3 and the stack) completely on the stack, high word first.
; The generic expansion passed four loose 16-bit values instead, and the
; callee read the wrong words.

; CHECK-LABEL: umulo16:
; CHECK:       rw r2, 1+r7
; CHECK:       rw r{{[0-9]}}, r7
; CHECK:       rj r4, __mulsi3
define i1 @umulo16(i16 %a, i16 %b) {
  %r = call { i16, i1 } @llvm.umul.with.overflow.i16(i16 %a, i16 %b)
  %o = extractvalue { i16, i1 } %r, 1
  ret i1 %o
}

; CHECK-LABEL: smulo16:
; CHECK:       rj r4, __mulsi3
define i1 @smulo16(i16 %a, i16 %b) {
  %r = call { i16, i1 } @llvm.smul.with.overflow.i16(i16 %a, i16 %b)
  %o = extractvalue { i16, i1 } %r, 1
  ret i1 %o
}

declare { i16, i1 } @llvm.umul.with.overflow.i16(i16, i16)
declare { i16, i1 } @llvm.smul.with.overflow.i16(i16, i16)
