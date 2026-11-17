; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj | llvm-objdump -dr --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mera400-unknown-none -filetype=obj < %s | llvm-objdump -dr --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ

; A comparison feeding a branch becomes one compare plus one or two jumps; the
; condition flags never live across instructions. "cw" compares signed, "cl"
; unsigned, "cwt" is the signed compare against a constant in -63..63.

declare void @a()
declare void @b()

; CHECK-LABEL: eq:
; CHECK:       cw r1, r2
; CHECK-NEXT:  jn
define void @eq(i16 %x, i16 %y) {
  %c = icmp eq i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; "jn" has no short form.
; CHECK-LABEL: ne:
; CHECK:       cw r1, r2
; CHECK-NEXT:  jes
define void @ne(i16 %x, i16 %y) {
  %c = icmp ne i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: slt:
; CHECK:       cw r1, r2
; CHECK-NEXT:  jgs
; CHECK-NEXT:  jes
define void @slt(i16 %x, i16 %y) {
  %c = icmp slt i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: sgt:
; CHECK:       cw r1, r2
; CHECK-NEXT:  jls
; CHECK-NEXT:  jes
define void @sgt(i16 %x, i16 %y) {
  %c = icmp sgt i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; The true block follows, so the condition is inverted and the branch goes to
; the false block: the original "<=" and ">=" become one jump, "<" and ">" two.
; CHECK-LABEL: sle:
; CHECK:       cw r1, r2
; CHECK-NEXT:  jgs
define void @sle(i16 %x, i16 %y) {
  %c = icmp sle i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: sge:
; CHECK:       cw r1, r2
; CHECK-NEXT:  jls
define void @sge(i16 %x, i16 %y) {
  %c = icmp sge i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: ult:
; CHECK:       cl r1, r2
; CHECK-NEXT:  jgs
; CHECK-NEXT:  jes
define void @ult(i16 %x, i16 %y) {
  %c = icmp ult i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: ugt:
; CHECK:       cl r1, r2
; CHECK-NEXT:  jls
; CHECK-NEXT:  jes
define void @ugt(i16 %x, i16 %y) {
  %c = icmp ugt i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: ule:
; CHECK:       cl r1, r2
; CHECK-NEXT:  jgs
define void @ule(i16 %x, i16 %y) {
  %c = icmp ule i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: uge:
; CHECK:       cl r1, r2
; CHECK-NEXT:  jls
define void @uge(i16 %x, i16 %y) {
  %c = icmp uge i16 %x, %y
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; Constants: "cwt" covers -63..63 for signed and equality compares, outside
; it and for every unsigned compare the two-word form is used.
; CHECK-LABEL: cwt_max:
; CHECK:       cwt r1, 63
define void @cwt_max(i16 %x) {
  %c = icmp eq i16 %x, 63
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: cwt_min:
; CHECK:       cwt r1, -63
define void @cwt_min(i16 %x) {
  %c = icmp eq i16 %x, -63
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: cw_64:
; CHECK:       cw r1, 64
define void @cw_64(i16 %x) {
  %c = icmp eq i16 %x, 64
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; CHECK-LABEL: cw_m64:
; CHECK:       cw r1, -64
define void @cw_m64(i16 %x) {
  %c = icmp eq i16 %x, -64
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; An unsigned compare with a small constant still needs "cl".
; CHECK-LABEL: cl_const:
; CHECK:       cl r1, 4
; CHECK-NEXT:  jgs
define void @cl_const(i16 %x) {
  %c = icmp ult i16 %x, 5
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; A constant on the left is swapped to the right.
; CHECK-LABEL: const_lhs:
; CHECK:       cwt r1, 4
; CHECK-NEXT:  jgs
define void @const_lhs(i16 %x) {
  %c = icmp sgt i16 5, %x
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; A loop: the back edge is "<=" so both jl and je go to the header, and the
; exit falls through. The counter is compared with the unsigned bound.
; CHECK-LABEL: loop_sle:
; CHECK:       awt r1, 1
; CHECK-NEXT:  cw r1, r2
; CHECK-NEXT:  jls .LBB{{[0-9]+}}_1
; CHECK-NEXT:  jes .LBB{{[0-9]+}}_1
define i16 @loop_sle(i16 %n) {
entry:
  br label %loop
loop:
  %i = phi i16 [ 0, %entry ], [ %next, %loop ]
  %next = add i16 %i, 1
  %c = icmp sle i16 %next, %n
  br i1 %c, label %loop, label %out
out:
  ret i16 %next
}

; CHECK-LABEL: loop_uge:
; CHECK:       cl r1, r2
; CHECK-NEXT:  jgs .LBB{{[0-9]+}}_1
; CHECK-NEXT:  jes .LBB{{[0-9]+}}_1
define i16 @loop_uge(i16 %n, i16 %m) {
entry:
  br label %loop
loop:
  %i = phi i16 [ %n, %entry ], [ %next, %loop ]
  %next = add i16 %i, -1
  %c = icmp uge i16 %next, %m
  br i1 %c, label %loop, label %out
out:
  ret i16 %next
}

; A branch on a value tests it against zero.
; CHECK-LABEL: on_value:
; CHECK:       cwt r1, 0
; CHECK-NEXT:  jes
define void @on_value(i16 zeroext %x) {
  %c = trunc i16 %x to i1
  br i1 %c, label %t, label %f
t:
  call void @a()
  ret void
f:
  call void @b()
  ret void
}

; Block placement puts the true block first: the false edge jumps over the
; other with an unconditional short jump, ending in "ujs".
; CHECK-LABEL: both_far:
; CHECK:       jn
; CHECK:       ujs
define void @both_far(i16 %x, i16 %y) {
entry:
  %c = icmp eq i16 %x, %y
  br i1 %c, label %t, label %f
f:
  call void @b()
  br label %end
t:
  call void @a()
  br label %end
end:
  ret void
}

; OBJ-LABEL: <sle>:
; OBJ:       cw r1, r2
; OBJ-NEXT:  jgs
; OBJ:       cw r1, 64
; OBJ-LABEL: <loop_sle>:
; OBJ:       cw r1, r2
; OBJ-NEXT:  jls
; OBJ-NEXT:  jes
