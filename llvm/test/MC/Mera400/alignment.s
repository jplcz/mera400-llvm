; REQUIRES: mera400-registered-target
; RUN: split-file %s %t
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/aligned.s -o %t.o
; RUN: llvm-readobj --sections --section-data --symbols %t.o | FileCheck %s --check-prefix=ALIGNED
; RUN: not llvm-mc -triple=mera400-unknown-none -filetype=obj %t/unaligned.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNALIGNED

;--- aligned.s
	.section .custom,"ax"
	.byte 0
	.p2align 1
first:
	lwt r1, 0
	.p2align 3
second:
	lwt r2, 0

; ALIGNED: Name: .custom
; ALIGNED: AddressAlignment: 8
; ALIGNED: SectionData (
; ALIGNED-NEXT: 0000: 0000D440 E000E000 D480
; ALIGNED: Name: first
; ALIGNED-NEXT: Value: 0x2
; ALIGNED: Name: second
; ALIGNED-NEXT: Value: 0x8

;--- unaligned.s
	.byte 0
before: lwt r1, 0
; UNALIGNED: unaligned.s:2:9: error: instruction must be 2-byte aligned
branch: ujs -1
; UNALIGNED: unaligned.s:4:9: error: instruction must be 2-byte aligned
load: lws r1, 1
; UNALIGNED: unaligned.s:6:7: error: instruction must be 2-byte aligned
count: irb r1, -1
; UNALIGNED: unaligned.s:8:8: error: instruction must be 2-byte aligned
shift: slz r1
; UNALIGNED: unaligned.s:10:8: error: instruction must be 2-byte aligned
	.section .custom,"ax"
	.byte 0
other: lwt r2, 0
; UNALIGNED: unaligned.s:14:8: error: instruction must be 2-byte aligned
