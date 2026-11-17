; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -show-encoding %s | FileCheck %s --check-prefix=ENC
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --sections --section-data --relocations %t | FileCheck %s --check-prefix=OBJ

; UJS boundary encodings are checked against the EMAS short acceptance fixture:
; https://github.com/jakubfi/emas/tree/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4/tests/acceptance/args

	ujs 0
; ENC: ujs 0{{.*}}encoding: [0xe0,0x00]
	ujs 1
; ENC: ujs 1{{.*}}encoding: [0xe0,0x01]
	ujs -1
; ENC: ujs -1{{.*}}encoding: [0xe2,0x01]
	ujs 63
; ENC: ujs 63{{.*}}encoding: [0xe0,0x3f]
	ujs -63
; ENC: ujs -63{{.*}}encoding: [0xe2,0x3f]
	nop
; ENC: nop{{.*}}encoding: [0xe0,0x00]
	jls -1
; ENC: jls -1{{.*}}encoding: [0xe2,0x41]
	jes -1
; ENC: jes -1{{.*}}encoding: [0xe2,0x81]
	jgs -1
; ENC: jgs -1{{.*}}encoding: [0xe2,0xc1]
	jvs -1
; ENC: jvs -1{{.*}}encoding: [0xe3,0x01]
	jxs -1
; ENC: jxs -1{{.*}}encoding: [0xe3,0x41]
	jys -1
; ENC: jys -1{{.*}}encoding: [0xe3,0x81]
	jcs -1
; ENC: jcs -1{{.*}}encoding: [0xe3,0xc1]

; OBJ: Name: .text
; OBJ: Size: 26
; OBJ: SectionData (
; OBJ-NEXT: 0000: E000E001 E201E03F E23FE000 E241E281
; OBJ-NEXT: 0010: E2C1E301 E341E381 E3C1
; OBJ: Relocations [
; OBJ-NEXT: ]
