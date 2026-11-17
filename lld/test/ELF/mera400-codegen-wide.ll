; REQUIRES: mera400
; RUN: llc -mtriple=mera400-unknown-none %s -o %t.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t.s -o %t.o
; RUN: ld.lld -m mera400elf -Ttext=0x100 %t.o -o %t
; RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

;; A 32-bit addition linked into an executable. The high word of a 32-bit
;; value is at the lower address, so the carry of "aw" on the low words is
;; added by "ac" to the high words. x is at word address 147 (high) and 148
;; (low), y at 149 and 150, and the sum is stored at 151 and 152.
; CHECK-LABEL: <long_add>:
; CHECK:       tw r{{[0-9]}}, 150
; CHECK-NEXT:  tw r{{[0-9]}}, 149
; CHECK-NEXT:  tw r{{[0-9]}}, 148
; CHECK-NEXT:  tw r{{[0-9]}}, 147
; CHECK-NEXT:  aw r{{[0-9]}}, r{{[0-9]}}
; CHECK-NEXT:  ac r{{[0-9]}}, r{{[0-9]}}
; CHECK-NEXT:  rw r{{[0-9]}}, 152
; CHECK-NEXT:  rw r{{[0-9]}}, 151

target triple = "mera400-unknown-none"
@x = global i32 65535
@y = global i32 1
@sum = global i32 0
define void @long_add() {
  %a = load i32, ptr @x
  %b = load i32, ptr @y
  %r = add i32 %a, %b
  store i32 %r, ptr @sum
  ret void
}
