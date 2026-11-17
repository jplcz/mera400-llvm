; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj | llvm-objdump -d --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ

; select and setcc become a small branch diamond with a PHI (there is no
; conditional move); i1 values are kept as 0 or 1 in a 16-bit register.

; CHECK-LABEL: sel:
; CHECK:       cw r1, r2
; CHECK:       jgs
; CHECK:       lw
define i16 @sel(i16 %x, i16 %y, i16 %a, i16 %b) {
  %c = icmp sgt i16 %x, %y
  %r = select i1 %c, i16 %a, i16 %b
  ret i16 %r
}

; CHECK-LABEL: setcc:
; CHECK:       lwt r{{[0-9]}}, 1
; CHECK-NEXT:  cw r{{[0-9]}}, r2
; CHECK-NEXT:  jls
; CHECK:       lwt r{{[0-9]}}, 0
define i16 @setcc(i16 %x, i16 %y) {
  %c = icmp slt i16 %x, %y
  %r = zext i1 %c to i16
  ret i16 %r
}

; "x < 0" is just the sign bit, which a shift now extracts.
; CHECK-LABEL: sign_bit:
; CHECK:       shc r1, 15
; CHECK-NEXT:  nr r1, 1
define i16 @sign_bit(i16 %x) {
  %c = icmp slt i16 %x, 0
  %r = zext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: smax:
; CHECK:       cw
define i16 @smax(i16 %x, i16 %y) {
  %r = call i16 @llvm.smax.i16(i16 %x, i16 %y)
  ret i16 %r
}

; CHECK-LABEL: umin:
; CHECK:       cl
define i16 @umin(i16 %x, i16 %y) {
  %r = call i16 @llvm.umin.i16(i16 %x, i16 %y)
  ret i16 %r
}

; CHECK-LABEL: sext_i1:
define i16 @sext_i1(i16 %x) {
  %c = icmp ne i16 %x, 0
  %r = sext i1 %c to i16
  ret i16 %r
}

; CHECK-LABEL: sel_i1:
define i1 @sel_i1(i1 %c, i1 %a, i1 %b) {
  %r = select i1 %c, i1 %a, i1 %b
  ret i1 %r
}

declare i16 @llvm.smax.i16(i16, i16)
declare i16 @llvm.umin.i16(i16, i16)

; OBJ-LABEL: <sel>:
; OBJ:       cw r1, r2
