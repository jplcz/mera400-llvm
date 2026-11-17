; REQUIRES: mera400
; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=mera400-unknown-none %t/a.ll -o %t/a.s
; RUN: llc -mtriple=mera400-unknown-none %t/b.ll -o %t/b.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/a.s -o %t/a.o
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/b.s -o %t/b.o
; RUN: ld.lld -m mera400elf -Ttext=0x100 %t/a.o %t/b.o -o %t/out
; RUN: llvm-objdump -d --no-show-raw-insn %t/out | FileCheck %s

;; Compiler output with branches, linked. Short jumps ("jls", "jes") count
;; words from the next instruction and are resolved by the assembler within
;; one section; the long jump ("uj" with an extra word) in the third function
;; carries a WORD16 relocation that the linker resolves to a word address
;; (the code starts at byte 0x100, i.e. word 0x80).
; CHECK-LABEL: <count>:
; CHECK-NEXT:  100: lw r2, r1
; CHECK-NEXT:  102: lwt r1, 0
; CHECK-LABEL: <count.LBB0_1>:
; CHECK-NEXT:  104: awt r1, 1
; CHECK-NEXT:  106: cw r1, r2
; CHECK-NEXT:  108: jls -3 <count.LBB0_1>
; CHECK-NEXT:  10a: jes -4 <count.LBB0_1>
; CHECK-NEXT:  10c: uj r4
; CHECK-LABEL: <skip>:
; CHECK:       116: jes 2 <skip.LBB1_2>
; CHECK-NEXT:  118: rj r4, 135
;; The loop in "big" is too long for a short jump: "uj" holds the word
;; address of the loop header, resolved by the linker.
; CHECK:       124: jn {{[0-9]+}} <big.LBB2_1>
; CHECK:       uj {{[0-9]+}} <big.LBB2_1>

;--- a.ll
target triple = "mera400-unknown-none"
define i16 @count(i16 %n) {
entry:
  br label %loop
loop:
  %i = phi i16 [ 0, %entry ], [ %next, %loop ]
  %next = add i16 %i, 1
  %c = icmp sle i16 %next, %n
  br i1 %c, label %loop, label %out
out:
  ret i16 %next
}

;--- b.ll
target triple = "mera400-unknown-none"
define void @sink() {
  ret void
}
define void @skip(i16 %x) {
entry:
  %c = icmp ne i16 %x, 0
  br i1 %c, label %t, label %f
t:
  call void @sink()
  ret void
f:
  ret void
}

@g = global i16 0
define void @big(i16 %x) {
entry:
  br label %head
head:
  %c = icmp eq i16 %x, 0
  br i1 %c, label %exit, label %body
body:
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  store volatile i16 %x, ptr @g
  br label %head
exit:
  ret void
}
