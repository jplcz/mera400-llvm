; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --hex-dump=.text %t | FileCheck %s

; A count defined later is resolved at layout, also with a fill value.
; CHECK: 0x00000000 00070007 0007
	.res n, 7
	.equ n, 3
