; REQUIRES: mera400-registered-target
; RUN: not llvm-mc -triple=mera400-unknown-none %s 2>&1 | FileCheck %s

; CHECK: error: Value 65536 is not an 16-bit signed/unsigned integer
.word 65535 + 1
; CHECK: error: Division by 0
.word 10 / 0
; CHECK: error: scale amount -1 is out of range [0, 15]
.word 1 \ -1
