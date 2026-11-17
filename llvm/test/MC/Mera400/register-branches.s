; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -show-encoding %s | FileCheck %s --check-prefix=ENC
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --sections --section-data --relocations %t | FileCheck %s --check-prefix=OBJ

; Opcodes are checked against the EMAS instruction table:
; https://github.com/jakubfi/emas/blob/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4/src/keywords.c
; Operand encoding is checked against its short_r acceptance fixture:
; https://github.com/jakubfi/emas/tree/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4/tests/acceptance/args

	trb r0, 0
; ENC: trb r0, 0{{.*}}encoding: [0xc4,0x00]
	trb r1, -1
; ENC: trb r1, -1{{.*}}encoding: [0xc6,0x41]
	trb r7, 63
; ENC: trb r7, 63{{.*}}encoding: [0xc5,0xff]
	trb r4, -63
; ENC: trb r4, -63{{.*}}encoding: [0xc7,0x3f]
	irb r0, 0
; ENC: irb r0, 0{{.*}}encoding: [0xc8,0x00]
	irb r1, -1
; ENC: irb r1, -1{{.*}}encoding: [0xca,0x41]
	IRB R7, (60 + 3)
; ENC: irb r7, 63{{.*}}encoding: [0xc9,0xff]
	drb r2, -63
; ENC: drb r2, -63{{.*}}encoding: [0xce,0xbf]
	drb r6, 63
; ENC: drb r6, 63{{.*}}encoding: [0xcd,0xbf]
	lwt r1, -3
; ENC: lwt r1, -3{{.*}}encoding: [0xd6,0x43]
	irb r1, -1
; ENC: irb r1, -1{{.*}}encoding: [0xca,0x41]

; OBJ: Name: .text
; OBJ: Size: 22
; OBJ: SectionData (
; OBJ-NEXT: 0000: C400C641 C5FFC73F C800CA41 C9FFCEBF
; OBJ-NEXT: 0010: CDBFD643 CA41
; OBJ: Relocations [
; OBJ-NEXT: ]
