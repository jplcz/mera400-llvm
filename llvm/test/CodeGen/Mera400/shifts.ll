; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj | llvm-objdump -d --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ

; Constant shifts of 16-bit values. The hardware shifts by one bit (slz, srz,
; and sxu+srx for an arithmetic shift) or rotates right by a count (shc).
; "shc r, n" rotates RIGHT by n, so a left shift by n is a rotate by -n (16 - n)
; followed by a mask that clears the bits that wrapped around.

; CHECK-LABEL: shl1:
; CHECK:       slz r1
; CHECK-NEXT:  uj r4
define i16 @shl1(i16 %x) {
  %r = shl i16 %x, 1
  ret i16 %r
}

; CHECK-LABEL: shl2:
; CHECK:       slz r1
; CHECK-NEXT:  slz r1
define i16 @shl2(i16 %x) {
  %r = shl i16 %x, 2
  ret i16 %r
}

; CHECK-LABEL: shl5:
; CHECK:       shc r1, -5
; CHECK-NEXT:  nr r1, -32
define i16 @shl5(i16 %x) {
  %r = shl i16 %x, 5
  ret i16 %r
}

; CHECK-LABEL: lshr1:
; CHECK:       srz r1
define i16 @lshr1(i16 %x) {
  %r = lshr i16 %x, 1
  ret i16 %r
}

; CHECK-LABEL: lshr3:
; CHECK:       shc r1, 3
; CHECK-NEXT:  nr r1, 8191
define i16 @lshr3(i16 %x) {
  %r = lshr i16 %x, 3
  ret i16 %r
}

; CHECK-LABEL: ashr1:
; CHECK:       sxu r1
; CHECK-NEXT:  srx r1
define i16 @ashr1(i16 %x) {
  %r = ashr i16 %x, 1
  ret i16 %r
}

; CHECK-LABEL: ashr2:
; CHECK:       sxu r1
; CHECK-NEXT:  srx r1
; CHECK-NEXT:  sxu r1
; CHECK-NEXT:  srx r1
define i16 @ashr2(i16 %x) {
  %r = ashr i16 %x, 2
  ret i16 %r
}

; Shifting by 15 leaves only copies of the sign bit.
; CHECK-LABEL: ashr15:
; CHECK-NOT:   rj
define i16 @ashr15(i16 %x) {
  %r = ashr i16 %x, 15
  ret i16 %r
}

; An arithmetic shift by 3 to 14 has no short sequence: it is a library call.
; CHECK-LABEL: ashr5:
; CHECK:       lwt r2, 5
; CHECK-NEXT:  rj r4, __ashrhi3
define i16 @ashr5(i16 %x) {
  %r = ashr i16 %x, 5
  ret i16 %r
}

; Rotates are one instruction.
; CHECK-LABEL: rotl3:
; CHECK:       shc r1, -3
; CHECK-NEXT:  uj r4
define i16 @rotl3(i16 %x) {
  %r = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 3)
  ret i16 %r
}
declare i16 @llvm.fshl.i16(i16, i16, i16)

; CHECK-LABEL: bswap:
; CHECK:       shc r1, 8
define i16 @bswap(i16 %x) {
  %r = call i16 @llvm.bswap.i16(i16 %x)
  ret i16 %r
}
declare i16 @llvm.bswap.i16(i16)

; A 32-bit shift by one chains the bit that falls out through the Y flag:
; the low limb goes first when shifting left, the high limb when right.
; CHECK-LABEL: shl32_1:
; CHECK:       slz r2
; CHECK-NEXT:  sly r1
define i32 @shl32_1(i32 %x) {
  %r = shl i32 %x, 1
  ret i32 %r
}

; CHECK-LABEL: lshr32_1:
; CHECK:       srz r1
; CHECK-NEXT:  sry r2
define i32 @lshr32_1(i32 %x) {
  %r = lshr i32 %x, 1
  ret i32 %r
}

; CHECK-LABEL: ashr32_1:
; CHECK:       sxu r1
; CHECK-NEXT:  srx r1
; CHECK-NEXT:  sry r2
define i32 @ashr32_1(i32 %x) {
  %r = ashr i32 %x, 1
  ret i32 %r
}

; A shift by 16 just moves limbs.
; CHECK-LABEL: shl32_16:
; CHECK:       lw r1, r2
; CHECK-NEXT:  lwt r2, 0
define i32 @shl32_16(i32 %x) {
  %r = shl i32 %x, 16
  ret i32 %r
}

; CHECK-LABEL: lshr32_16:
; CHECK:       lw r2, r1
; CHECK-NEXT:  lwt r1, 0
define i32 @lshr32_16(i32 %x) {
  %r = lshr i32 %x, 16
  ret i32 %r
}

; CHECK-LABEL: ashr32_16:
; CHECK:       lw r2, r1
; CHECK:       sxu
define i32 @ashr32_16(i32 %x) {
  %r = ashr i32 %x, 16
  ret i32 %r
}

; Sign extension of a byte: flip the sign bit and subtract it again.
; CHECK-LABEL: sext8:
; CHECK:       nr r1, 255
; CHECK-NEXT:  xr r1, 128
; CHECK-NEXT:  aw r1, -128
define i16 @sext8(i16 %x) {
  %t = trunc i16 %x to i8
  %r = sext i8 %t to i16
  ret i16 %r
}

; CHECK-LABEL: sext8_load:
; CHECK:       lwt r2, 0
; CHECK-NEXT:  lb r2, r1
; CHECK-NEXT:  xr r2, 128
; CHECK-NEXT:  aw r2, -128
define i16 @sext8_load(ptr addrspace(1) %p) {
  %v = load i8, ptr addrspace(1) %p
  %r = sext i8 %v to i16
  ret i16 %r
}

; OBJ-LABEL: <shl1>:
; OBJ:       slz r1
; OBJ:       shc r1, -5
; OBJ:       nr r1, -32
; OBJ:       shc r1, 3
; OBJ:       sxu r1
; OBJ-NEXT:  srx r1
