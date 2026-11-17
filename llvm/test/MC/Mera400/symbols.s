; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --hex-dump=.text %t | FileCheck %s
; RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=REL
; RUN: not llvm-mc -triple=mera400-unknown-none -filetype=obj %S/Inputs/symbol-errors.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

; Expected words come from the EMAS acceptance tests (expr, labels, pragmas,
; args). Label values are word addresses, so a label at byte 4 is 2.
; CHECK: 0x00000000 00000001 00020003 000a0005 00000005
; CHECK-NEXT: 0x00000010 00630009 000a0002 00010001 e0000001
; Label words are relocated against the section symbol (the word offset stays
; in place) and the external symbol keeps its own relocation.
; REL: .rel.text {
; REL-NEXT: 0x0 R_MERA400_WORD16 .text
; REL: 0x1E R_MERA400_WORD16 external

first:
	.word first, first + 1
.loop:
	.word .loop, 3
; .equ has no comma, redefinition is allowed and later uses see the latest value
	.equ val 10
	.word val
second:
	.word second
	.equ val 0
	.word val
; label differences are constants
	.word second - first
; a constant defined later is resolved at the end
	.word fwd
	.word .
	.equ fwd 99
	.word .
	.struct point:
x:	.res 1
y:	.res 1
	.endstruct
	.word point, y
	.ifdef fwd
	.word 1
	.else
	.word 2
	.endif
	ujs target
target:
	.word external + 1

; ERR: error: Symbol 'a' already defined
; ERR: error: Cannot use local label ".local" outside a global label context
; ERR: error: Cannot define local label ".local" outside a global label context
; ERR: error: .org argument must be a constant expression (no forward references)
; ERR: error: Instruction valid only for MX-16
; ERR: error: syntax error, unexpected symbol, expecting label
; ERR: error: shift amount 16 is out of range [0, 15]
; ERR: error: operator '*' requires constant expressions
