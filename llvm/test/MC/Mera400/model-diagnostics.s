; REQUIRES: mera400-registered-target
; RUN: not llvm-mc -triple=mera400-unknown-none -filetype=asm %s 2>&1 | FileCheck %s
; RUN: printf '.model small\n.model large\n' | not llvm-mc -triple=mera400-unknown-none -filetype=asm 2>&1 | FileCheck %s --check-prefix=CONFLICT
; RUN: printf 'lw r1, hi(g+70000)\ng: .byte 0\n' | not llvm-mc -triple=mera400-unknown-none -filetype=obj -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANGE

; CONFLICT: error: memory model already set to small
; RANGE: error: expected relocatable expression

	.model huge
; CHECK: error: Unknown memory model 'huge' (expected small or large)
	.model
; CHECK: error: expected memory model (small or large)
	lw r1, hi(g)+1
; CHECK: error: hi() and lo() cannot be used in an expression
	lw r1, -lo(g)
; CHECK: error: hi() and lo() cannot be used in an expression
	lw r1, hi(g
; CHECK: error: expected ')'
