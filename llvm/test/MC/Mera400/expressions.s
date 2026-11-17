; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none %s | FileCheck %s

; Expected values come from the EMAS acceptance tests (expr/precedence, uminus).
; CHECK: .short 6
; CHECK: .short 6
; CHECK: .short 8
; CHECK: .short 6
; CHECK: .short 65535
; CHECK: .short 6
; CHECK: .short 32769
.word 2 * 2 + 2, 2 + 2 * 2, (2 + 2) * 2, 1<<1 | 1<<2, ~1 | 1, 1\14 + 1\13
.word -32767
