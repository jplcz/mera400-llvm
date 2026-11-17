; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj | llvm-objdump -d --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ

; 32- and 64-bit integers are split into 16-bit limbs, the HIGH limb first:
; an i32 argument or result is R1 (high) : R2 (low), and in memory the high
; limb is at the lower address. The carry is passed between limbs through a
; 0/1 value, but a 32-bit add or subtract uses the real carry flag with
; "aw lo; ac hi", which is the only place it is relied upon.

; CHECK-LABEL: add32:
; CHECK:       tw r3, 2+r7
; CHECK:       aw r2, r3
; CHECK-NEXT:  ac r1, r5
define i32 @add32(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: sub32:
; CHECK:       sw r
; CHECK:       ngl r
; CHECK:       ac r1
define i32 @sub32(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: neg32:
define i32 @neg32(i32 %a) {
  %r = sub i32 0, %a
  ret i32 %r
}

; CHECK-LABEL: logic32:
; CHECK:       or r2, 4660
; CHECK-NEXT:  lwt r1, -1
define i32 @logic32(i32 %a, i32 %b) {
  %x = and i32 %a, 65535
  %y = or i32 %x, 4660
  %z = xor i32 %y, -65536
  ret i32 %z
}

; The high limb is loaded and stored at the lower address.
; CHECK-LABEL: load32:
; CHECK:       tw r3, r1
; CHECK-NEXT:  tw r2, 1+r1
; CHECK-NEXT:  lw r1, r3
define i32 @load32(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}

; CHECK-LABEL: store32:
; CHECK:       rw r3, 1+r1
; CHECK-NEXT:  rw r2, r1
define void @store32(ptr %p, i32 %v) {
  store i32 %v, ptr %p
  ret void
}

; The second word of a 64-bit value is word 1, not byte 1.
; CHECK-LABEL: load64:
; CHECK:       tw r1, 3+r1
define i16 @load64(ptr %p) {
  %v = load i64, ptr %p
  %t = trunc i64 %v to i16
  ret i16 %t
}

; A 64-bit result does not fit in registers: the caller passes a hidden
; pointer to the result in R1.
; CHECK-LABEL: add64:
; CHECK:       aw r
; CHECK:       ac r
; CHECK:       rw r{{[0-9]}}, 3+r2
; CHECK:       rw r{{[0-9]}}, r2
define i64 @add64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: sub64:
define i64 @sub64(i64 %a, i64 %b) {
  %r = sub i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: zext32:
; CHECK:       lwt r1, 0
; CHECK-NEXT:  uj r4
define i32 @zext32(i16 %a) {
  %r = zext i16 %a to i32
  ret i32 %r
}

; CHECK-LABEL: sext32:
; CHECK:       sxu r2
; CHECK-NEXT:  lwt r1, 0
; CHECK-NEXT:  slx r1
; CHECK-NEXT:  nga r1
define i32 @sext32(i16 %a) {
  %r = sext i16 %a to i32
  ret i32 %r
}

; CHECK-LABEL: trunc32:
define i16 @trunc32(i32 %a) {
  %r = trunc i32 %a to i16
  ret i16 %r
}

; CHECK-LABEL: call32:
; CHECK:       rj r4, ext32
; CHECK:       aw r2, r
; CHECK-NEXT:  ac r1, r
declare i32 @ext32(i32, i16)
define i32 @call32(i32 %a, i16 %b) {
  %r = call i32 @ext32(i32 %a, i16 %b)
  %s = add i32 %r, 1
  ret i32 %s
}

; CHECK-LABEL: sel32:
; CHECK:       cwt r3, 0
; CHECK:       jes
define i32 @sel32(i32 %a, i32 %b, i16 %c) {
  %t = icmp eq i16 %c, 0
  %r = select i1 %t, i32 %a, i32 %b
  ret i32 %r
}

; The end-of-function labels become symbols, so only the first function is
; found by name.
; OBJ-LABEL: <add32>:
; OBJ:       aw r2, r3
; OBJ-NEXT:  ac r1, r5
; OBJ:       tw r3, r1
; OBJ-NEXT:  tw r2, 1+r1
