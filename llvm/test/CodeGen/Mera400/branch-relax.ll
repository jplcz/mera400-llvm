; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj | llvm-objdump -d --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ

; Short jumps ("ujs", "jls", ...) reach 63 words from the end of the jump
; instruction; a branch whose target is further away is relaxed: a conditional
; branch becomes the inverted condition over a long "uj", an unconditional one
; becomes "uj" with a 16-bit word address. Each "rw r1, g" is two words.

@g = global i16 0

; A skipped region of 10 words stays short.
; CHECK-LABEL: near:
; CHECK:       jls .LBB0_2
; CHECK-NOT:   uj .LBB
; OBJ-LABEL:   <near>:
; OBJ:         jls
define void @near(i16 %a, i16 %b) {
  %c = icmp slt i16 %a, %b
  br i1 %c, label %far, label %mid
mid:
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  br label %far
far:
  ret void
}

; 70 stores are 140 words: both the compare and the jump over them are relaxed.
; CHECK-LABEL: far_cond:
; CHECK:       cw r1, r2
; CHECK-NEXT:  jgs .LBB1_1
; CHECK-NEXT:  jes .LBB1_1
; CHECK-NEXT:  ; %bb.
; CHECK-NEXT:  uj .LBB1_2
; CHECK-NEXT:  .LBB1_1:
; OBJ:         jgs{{.*}}<far_cond.LBB1_1>
; OBJ-NEXT:    jes
; OBJ-NEXT:    uj
define void @far_cond(i16 %a, i16 %b) {
  %c = icmp slt i16 %a, %b
  br i1 %c, label %far, label %mid
mid:
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  br label %far
far:
  ret void
}

; The back edge of a long loop is an unconditional jump that is relaxed.
; CHECK-LABEL: far_uncond:
; CHECK:       uj .LBB2_1
; OBJ:         uj{{.*}}<far_uncond.LBB2_1>
define void @far_uncond(i16 %a) {
entry:
  br label %head
head:
  %c = icmp eq i16 %a, 0
  br i1 %c, label %exit, label %body
body:
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  store volatile i16 %a, ptr @g
  br label %head
exit:
  ret void
}
