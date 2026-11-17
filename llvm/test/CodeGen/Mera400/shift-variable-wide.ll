; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | %python %S/Inputs/shift-wide-sim.py small | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -O0 < %s | %python %S/Inputs/shift-wide-sim.py small | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large < %s | %python %S/Inputs/shift-wide-sim.py large | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -O0 < %s | %python %S/Inputs/shift-wide-sim.py large | FileCheck --check-prefix=SIM %s

; Variable-count shifts of 32 and 64-bit values are calls to the builtins
; routines (__ashlsi3, __ashldi3, ...). They used to be expanded through a
; temporary on the stack that is addressed in bytes, but a pointer counts
; words here, so the temporary's two halves were a word-count apart too far:
; the stores overwrote the spill slots (r7 and the return address), and the
; result was wrong. No memory temporary may be used, so the only calls are the
; library ones, and the frame is no bigger than what the calls need (the
; simulator checks that nothing outside it, and no saved register, is hit).

; CHECK-LABEL: shl64:
; CHECK:       rj r4, __ashldi3
define i64 @shl64(i64 %a, i16 %n) {
  %c = zext i16 %n to i64
  %r = shl i64 %a, %c
  ret i64 %r
}

; CHECK-LABEL: lshr64:
; CHECK:       rj r4, __lshrdi3
define i64 @lshr64(i64 %a, i16 %n) {
  %c = zext i16 %n to i64
  %r = lshr i64 %a, %c
  ret i64 %r
}

; CHECK-LABEL: ashr64:
; CHECK:       rj r4, __ashrdi3
define i64 @ashr64(i64 %a, i16 %n) {
  %c = zext i16 %n to i64
  %r = ashr i64 %a, %c
  ret i64 %r
}

; CHECK-LABEL: shl32:
; CHECK:       rj r4, __ashlsi3
define i32 @shl32(i32 %a, i16 %n) {
  %c = zext i16 %n to i32
  %r = shl i32 %a, %c
  ret i32 %r
}

; CHECK-LABEL: lshr32:
; CHECK:       rj r4, __lshrsi3
define i32 @lshr32(i32 %a, i16 %n) {
  %c = zext i16 %n to i32
  %r = lshr i32 %a, %c
  ret i32 %r
}

; CHECK-LABEL: ashr32:
; CHECK:       rj r4, __ashrsi3
define i32 @ashr32(i32 %a, i16 %n) {
  %c = zext i16 %n to i32
  %r = ashr i32 %a, %c
  ret i32 %r
}

; SIM: wide shifts ok
