; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck --check-prefix=OBJ %s

; CHECK-LABEL: add:
; CHECK:       aw r1, r2
; CHECK-NEXT:  uj r4
; OBJ-LABEL: <add>:
; OBJ:       aw r1, r2
; OBJ-NEXT:  uj r4
define i16 @add(i16 %a, i16 %b) {
  %r = add i16 %a, %b
  ret i16 %r
}

; CHECK-LABEL: sub:
; CHECK:       sw r1, r2
define i16 @sub(i16 %a, i16 %b) {
  %r = sub i16 %a, %b
  ret i16 %r
}

; CHECK-LABEL: logic:
; CHECK:       nr r1, r2
; CHECK-NEXT:  or r1, r3
; CHECK-NEXT:  xr r1, r2
define i16 @logic(i16 %a, i16 %b, i16 %c) {
  %x = and i16 %a, %b
  %y = or i16 %x, %c
  %z = xor i16 %y, %b
  ret i16 %z
}

; CHECK-LABEL: short_imm:
; CHECK:       awt r1, 5
; CHECK-NEXT:  xr r1, r2
; CHECK-NEXT:  awt r1, -5
define i16 @short_imm(i16 %a, i16 %b) {
  %x = add i16 %a, 5
  %y = xor i16 %x, %b
  %w = sub i16 %y, 5
  ret i16 %w
}

; CHECK-LABEL: long_imm:
; CHECK:       aw r1, 1000
; CHECK-NEXT:  xr r1, 4660
define i16 @long_imm(i16 %a) {
  %x = add i16 %a, 1000
  %y = xor i16 %x, 4660
  ret i16 %y
}

; CHECK-LABEL: const_short:
; CHECK:       lwt r1, 7
define i16 @const_short() {
  ret i16 7
}

; CHECK-LABEL: const_long:
; CHECK:       lw r1, 1234
define i16 @const_long() {
  ret i16 1234
}

; CHECK-LABEL: copy:
; CHECK:       lw r1, r2
; CHECK-NEXT:  uj r4
define i16 @copy(i16 %a, i16 %b) {
  ret i16 %b
}

; CHECK-LABEL: ret_void:
; CHECK-NEXT: ; %bb.0:
; CHECK-NEXT:  uj r4
define void @ret_void() {
  ret void
}

; CHECK-LABEL: promoted_i8:
; CHECK:       aw r1, r2
define i8 @promoted_i8(i8 %a, i8 %b) {
  %r = add i8 %a, %b
  ret i8 %r
}
