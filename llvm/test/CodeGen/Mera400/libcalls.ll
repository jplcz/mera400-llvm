; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj | llvm-objdump -d --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ

; The hardware has no variable shift, multiply or divide that fits the
; calling convention, so they are calls to compiler-rt routines (__ashlhi3
; shifts left a "half" (16-bit) value, __mulsi3 multiplies "single" (32-bit)
; values, and so on).

; CHECK-LABEL: shl_var:
; CHECK:       rj r4, __ashlhi3
define i16 @shl_var(i16 %x, i16 %y) {
  %r = shl i16 %x, %y
  ret i16 %r
}

; CHECK-LABEL: lshr_var:
; CHECK:       rj r4, __lshrhi3
define i16 @lshr_var(i16 %x, i16 %y) {
  %r = lshr i16 %x, %y
  ret i16 %r
}

; CHECK-LABEL: ashr_var:
; CHECK:       rj r4, __ashrhi3
define i16 @ashr_var(i16 %x, i16 %y) {
  %r = ashr i16 %x, %y
  ret i16 %r
}

; CHECK-LABEL: mul16:
; CHECK:       rj r4, __mulhi3
define i16 @mul16(i16 %x, i16 %y) {
  %r = mul i16 %x, %y
  ret i16 %r
}

; CHECK-LABEL: sdiv16:
; CHECK:       rj r4, __divhi3
define i16 @sdiv16(i16 %x, i16 %y) {
  %r = sdiv i16 %x, %y
  ret i16 %r
}

; CHECK-LABEL: udiv16:
; CHECK:       rj r4, __udivhi3
define i16 @udiv16(i16 %x, i16 %y) {
  %r = udiv i16 %x, %y
  ret i16 %r
}

; CHECK-LABEL: srem16:
; CHECK:       rj r4, __modhi3
define i16 @srem16(i16 %x, i16 %y) {
  %r = srem i16 %x, %y
  ret i16 %r
}

; CHECK-LABEL: urem16:
; CHECK:       rj r4, __umodhi3
define i16 @urem16(i16 %x, i16 %y) {
  %r = urem i16 %x, %y
  ret i16 %r
}

; CHECK-LABEL: shl32_var:
; CHECK:       rj r4, __ashlsi3
define i32 @shl32_var(i32 %x, i32 %y) {
  %r = shl i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: mul32:
; CHECK:       rj r4, __mulsi3
define i32 @mul32(i32 %x, i32 %y) {
  %r = mul i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: sdiv32:
; CHECK:       rj r4, __divsi3
define i32 @sdiv32(i32 %x, i32 %y) {
  %r = sdiv i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: udiv32:
; CHECK:       rj r4, __udivsi3
define i32 @udiv32(i32 %x, i32 %y) {
  %r = udiv i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: srem32:
; CHECK:       rj r4, __modsi3
define i32 @srem32(i32 %x, i32 %y) {
  %r = srem i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: urem32:
; CHECK:       rj r4, __umodsi3
define i32 @urem32(i32 %x, i32 %y) {
  %r = urem i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: mul64:
; CHECK:       rj r4, __muldi3
define i64 @mul64(i64 %x, i64 %y) {
  %r = mul i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: sdiv64:
; CHECK:       rj r4, __divdi3
define i64 @sdiv64(i64 %x, i64 %y) {
  %r = sdiv i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: urem64:
; CHECK:       rj r4, __umoddi3
define i64 @urem64(i64 %x, i64 %y) {
  %r = urem i64 %x, %y
  ret i64 %r
}

; A multiplication by 2^k or 2^k +- 1 is expanded into shifts and an add or
; subtract, so no call is made.
; CHECK-LABEL: mul8:
; CHECK-NOT:   rj
; CHECK:       shc r1, -3
define i16 @mul8(i16 %x) {
  %r = mul i16 %x, 8
  ret i16 %r
}

; CHECK-LABEL: mul7:
; CHECK-NOT:   rj
; CHECK:       sw r
define i16 @mul7(i16 %x) {
  %r = mul i16 %x, 7
  ret i16 %r
}

; CHECK-LABEL: mul5:
; CHECK-NOT:   rj
; CHECK:       aw r
define i16 @mul5(i16 %x) {
  %r = mul i16 %x, 5
  ret i16 %r
}

; CHECK-LABEL: mul32_9:
; CHECK-NOT:   rj
define i32 @mul32_9(i32 %x) {
  %r = mul i32 %x, 9
  ret i32 %r
}

; Any other constant still calls the library.
; CHECK-LABEL: mul100:
; CHECK:       rj r4, __mulhi3
define i16 @mul100(i16 %x) {
  %r = mul i16 %x, 100
  ret i16 %r
}

; OBJ-LABEL: <shl_var>:
; OBJ:       rj r4, {{[0-9]+}}
