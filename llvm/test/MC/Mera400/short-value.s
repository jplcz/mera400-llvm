; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj -r --hex-dump=.text %t | FileCheck %s
; RUN: not llvm-mc -triple=mera400-unknown-none -filetype=obj %S/Inputs/short-value-errors.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

; Short values (awt, cwt, lwt, trb) may be labels: the operand is the word
; address, stored as a 7-bit sign-and-magnitude value. The addend is kept in
; the field (in words) and the linker adds the symbol address.
; CHECK: 0x0 R_MERA400_WORD7 .text
; CHECK-NEXT: 0x2 R_MERA400_WORD7 ext
; CHECK-NEXT: 0x4 R_MERA400_WORD7 ext
; CHECK-NEXT: 0x6 R_MERA400_WORD7 .text
; CHECK: 0x00000000 d441d040 c444d446 e000
	lwt r1, start
start:
	cwt r1, ext
	trb r1, ext+4
	lwt r1, here+2
here:
	nop
; ERR: error: short value is out of range [-63, 63] words or not word aligned
