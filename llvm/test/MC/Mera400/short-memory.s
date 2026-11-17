; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -show-encoding %s | FileCheck %s --check-prefix=ENC
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --sections --section-data --relocations %t | FileCheck %s --check-prefix=OBJ

; Opcode values are checked against the EMAS instruction table:
; https://github.com/jakubfi/emas/blob/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4/src/keywords.c
; The operand format is checked against the EMAS short_r acceptance fixture:
; https://github.com/jakubfi/emas/tree/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4/tests/acceptance/args

	lws r0, 0
; ENC: lws r0, 0{{.*}}encoding: [0xd8,0x00]
	lws r1, 1
; ENC: lws r1, 1{{.*}}encoding: [0xd8,0x41]
	lws r2, -1
; ENC: lws r2, -1{{.*}}encoding: [0xda,0x81]
	lws r3, 63
; ENC: lws r3, 63{{.*}}encoding: [0xd8,0xff]
	lws r4, -63
; ENC: lws r4, -63{{.*}}encoding: [0xdb,0x3f]
	rws r0, 0
; ENC: rws r0, 0{{.*}}encoding: [0xdc,0x00]
	rws r7, -63
; ENC: rws r7, -63{{.*}}encoding: [0xdf,0xff]
	RWS R5, (60 + 3)
; ENC: rws r5, 63{{.*}}encoding: [0xdd,0x7f]
	lws r1, 1
; ENC: lws r1, 1{{.*}}encoding: [0xd8,0x41]
	ujs 1
; ENC: ujs 1{{.*}}encoding: [0xe0,0x01]
	.short 0x1234
	nop
; ENC: nop{{.*}}encoding: [0xe0,0x00]

; OBJ: Name: .text
; OBJ: Size: 24
; OBJ: SectionData (
; OBJ-NEXT: 0000: D800D841 DA81D8FF DB3FDC00 DFFFDD7F
; OBJ-NEXT: 0010: D841E001 1234E000
; OBJ: Relocations [
; OBJ-NEXT: ]
