; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=asm %s | FileCheck %s --check-prefix=ASM
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t.o
; RUN: llvm-readobj -h -r %t.o | FileCheck %s --check-prefix=OBJ
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=asm %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t2.o
; RUN: cmp %t.o %t2.o
; RUN: printf '.model small\n.model small\n' | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t3.o
; RUN: llvm-readobj -h %t3.o | FileCheck %s --check-prefix=SMALL
; RUN: printf '.long 1\n' | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t4.o
; RUN: llvm-readobj -h %t4.o | FileCheck %s --check-prefix=NEUTRAL

; ASM: .model large
; OBJ: EF_MERA400_LARGE (0x1)
; SMALL: EF_MERA400_SMALL (0x2)
; NEUTRAL: Flags [ (0x0)

	.model large
	.text
f:
; The halves of a byte address: hi() and lo() take bytes, not words.
; ASM: lw r1, hi(g+3)
; ASM: lw r2, lo(g+3)
; ASM: lw r3, 0
; ASM: lw r4, 9029
	lw r1, hi(g+3)
	lw r2, lo(g+3)
	lw r3, hi(7)
	lw r4, lo(0x12345)

	.data
g:
; ASM: .long g+1
; ASM: .long 131075
	.long g+1
	.long 0x20003

; OBJ: R_MERA400_HI16 g
; OBJ: R_MERA400_LO16 g
; OBJ: R_MERA400_32 .data
