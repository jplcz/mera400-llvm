; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj -r --hex-dump=.text %t | FileCheck %s
; RUN: not llvm-mc -triple=mera400-unknown-none -filetype=obj %S/Inputs/short-offset-errors.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

; A branch to a label in another section keeps the label's offset: the field
; holds the addend in words (the label is 2 words into .data). An offset on an
; undefined symbol is kept the same way, as a signed word count.
; CHECK: 0x0 R_MERA400_PCREL7 .data
; CHECK-NEXT: 0x2 R_MERA400_PCREL7 ext
; CHECK-NEXT: 0x4 R_MERA400_PCREL7 ext
; The difference of two labels is a plain distance in words, not a target.
; CHECK: 0x00000000 e002e003 e203e001 e241
	ujs d
	ujs ext+3
	ujs ext-3
a:	ujs b-a
b:	jls a-b
	.data
	.word 1, 2
d:	nop
; ERR: error: short branch offset is out of range [-63, 63] words or not word aligned
