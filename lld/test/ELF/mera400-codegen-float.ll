; REQUIRES: mera400
; RUN: llc -mtriple=mera400-unknown-none -mattr=+awp %s -o %t.s
; RUN: llvm-mc -triple=mera400-unknown-none -mattr=+awp -filetype=obj %t.s -o %t.o
; RUN: ld.lld -m mera400elf -Ttext=0x100 %t.o -o %t
; RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s
; RUN: llvm-objdump -s -j .data %t | FileCheck --check-prefix=DATA %s
; RUN: llc -mtriple=mera400-unknown-none %s -o %t2.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t2.s -o %t2.o
; RUN: not ld.lld -m mera400elf -Ttext=0x100 %t2.o -o %t2 2>&1 | FileCheck --check-prefix=UNDEF %s

;; With the awp feature a double addition is the processor's "af" instruction,
;; so linking needs no runtime library. The global is stored in the native
;; format (three words and a zero padding word): 1.5 is the mantissa 0.75
;; (0x6000000000) and the exponent 1.
; CHECK-LABEL: <fadd_global>:
; CHECK:       af
; CHECK-NOT:   rj r4

; DATA: 60000000 00010000

;; Without it the addition is a call of the library function __m48_add, which
;; the linker reports as missing when it is not provided.
; UNDEF: undefined symbol: __m48_add

target triple = "mera400-unknown-none"
@x = global double 1.5
define double @fadd_global() {
  %a = load double, ptr @x
  %r = fadd double %a, %a
  ret double %r
}
