; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj | llvm-objdump -d --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ

; A wide comparison is a chain of branches: the high limbs are compared with
; the signedness of the predicate, and only when they are equal the low limbs
; are compared, always as unsigned numbers ("cl"). Constant operands are first
; loaded into registers.

; CHECK-LABEL: slt32:
; CHECK:       cw r{{[0-9]}}, r{{[0-9]}}
; CHECK-NEXT:  jls
; CHECK:       cw r{{[0-9]}}, r{{[0-9]}}
; CHECK-NEXT:  jn
; CHECK:       cl r2, r
define i16 @slt32(i32 %a, i32 %b) {
  %c = icmp slt i32 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: ult32:
; CHECK:       cl r{{[0-9]}}, r{{[0-9]}}
; CHECK-NEXT:  jls
; CHECK:       cl r2, r
define i16 @ult32(i32 %a, i32 %b) {
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: eq32:
; CHECK:       cw
; CHECK:       cw
define i16 @eq32(i32 %a, i32 %b) {
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: ne32:
define i16 @ne32(i32 %a, i32 %b) {
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: sge32:
define i16 @sge32(i32 %a, i32 %b) {
  %c = icmp sge i32 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: ugt32:
define i16 @ugt32(i32 %a, i32 %b) {
  %c = icmp ugt i32 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: ule32:
define i16 @ule32(i32 %a, i32 %b) {
  %c = icmp ule i32 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: ult64:
; CHECK:       cl
; CHECK:       cl
; CHECK:       cl
; CHECK:       cl
define i16 @ult64(i64 %a, i64 %b) {
  %c = icmp ult i64 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: slt64:
; CHECK:       cw
define i16 @slt64(i64 %a, i64 %b) {
  %c = icmp slt i64 %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: eq_const64:
define i16 @eq_const64(i64 %a) {
  %c = icmp eq i64 %a, 4294967296
  %r = zext i1 %c to i16
  ret i16 %r
}

; A wide compare feeding a branch.
; CHECK-LABEL: branch32:
; CHECK:       cw
; CHECK:       cl
declare void @a()
declare void @b()
define void @branch32(i32 %x, i32 %y) {
entry:
  %c = icmp sgt i32 %x, %y
  br i1 %c, label %then, label %else
then:
  call void @a()
  ret void
else:
  call void @b()
  ret void
}

; The true target may start with a PHI: every block of the chain that jumps
; there is a new predecessor.
; CHECK-LABEL: phi_target:
; CHECK:       lwt r{{[0-9]}}, 11
; CHECK:       lwt r{{[0-9]}}, 22
define i16 @phi_target(i32 %a, i32 %b) {
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %join, label %other
other:
  br label %join
join:
  %r = phi i16 [ 11, %entry ], [ 22, %other ]
  ret i16 %r
}

; A wide select.
; CHECK-LABEL: smax64:
; CHECK:       cw
define i64 @smax64(i64 %a, i64 %b) {
  %c = icmp sgt i64 %a, %b
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

; CHECK-LABEL: umin32:
; CHECK:       cl
define i32 @umin32(i32 %a, i32 %b) {
  %r = call i32 @llvm.umin.i32(i32 %a, i32 %b)
  ret i32 %r
}
declare i32 @llvm.umin.i32(i32, i32)

; OBJ-LABEL: <slt32>:
; OBJ:       cw r
; OBJ:       cl r2
