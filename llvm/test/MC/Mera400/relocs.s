; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj -r --hex-dump=.text %t | FileCheck %s
; RUN: not llvm-mc -triple=mera400-unknown-none -filetype=obj %S/Inputs/branch-errors.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

; A short branch to a label in the same section is resolved by the assembler:
; the offset counts words from the instruction after the branch. Branches to
; symbols defined elsewhere get a PCREL7 relocation for the linker.
; CHECK: 0x0 R_MERA400_PCREL7 ext
; CHECK-NEXT: 0x2 R_MERA400_WORD16 ext
; CHECK-NEXT: 0x8 R_MERA400_WORD16 ext
; CHECK: 0x00000000 e0000000 e000e000 0001e001 e000
	ujs ext
	.word ext
	ujs back
back:
	nop
	.word ext + 1
	ujs fwd
	nop
fwd:
; ERR: error: short branch target is out of range
