; REQUIRES: mera400
; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=mera400-unknown-none %t/a.ll -o %t/a.s
; RUN: llc -mtriple=mera400-unknown-none %t/b.ll -o %t/b.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/a.s -o %t/a.o
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/b.s -o %t/b.o
; RUN: ld.lld -m mera400elf -Ttext=0x100 %t/a.o %t/b.o -o %t/out
; RUN: llvm-objdump -d --no-show-raw-insn %t/out | FileCheck %s

;; Compiler output linked with the linker. "rj r4, f" is a two-word
;; instruction whose second word is a WORD16 relocation against f. The code
;; starts at byte 0x100, so callee at byte 0x10e is word address 0x87 (135),
;; and the global after the code, at byte 0x116, is word address 0x8b (139).
;; The compiler's end-of-function labels show up in the symbol table, so the
;; second function is found by its instructions.
; CHECK-LABEL: <caller>:
; CHECK-NEXT:  100: awt r7, -1
; CHECK-NEXT:  102: rw r4, r7
; CHECK-NEXT:  104: rj r4, 135
; CHECK-NEXT:  108: tw r4, r7
; CHECK-NEXT:  10a: awt r7, 1
; CHECK-NEXT:  10c: uj r4
; CHECK:       10e: tw r2, 139
; CHECK-NEXT:  112: aw r1, r2
; CHECK-NEXT:  114: uj r4

;--- a.ll
target triple = "mera400-unknown-none"
declare i16 @callee(i16)
define i16 @caller(i16 %a) {
  %r = call i16 @callee(i16 %a)
  ret i16 %r
}

;--- b.ll
target triple = "mera400-unknown-none"
@counter = global i16 5
define i16 @callee(i16 %a) {
  %v = load i16, ptr @counter
  %r = add i16 %a, %v
  ret i16 %r
}
