; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -show-encoding %s | FileCheck %s --check-prefix=ENC
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --sections --section-data --relocations %t | FileCheck %s --check-prefix=OBJ

; Opcodes are checked against the EMAS instruction table:
; https://github.com/jakubfi/emas/blob/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4/src/keywords.c

	nga r0
; ENC: nga r0{{.*}}encoding: [0xe8,0x03]
	ngc r0
; ENC: ngc r0{{.*}}encoding: [0xea,0x03]
	ngl r0
; ENC: ngl r0{{.*}}encoding: [0xe8,0x08]
	zlb r0
; ENC: zlb r0{{.*}}encoding: [0xe8,0x01]
	zrb r0
; ENC: zrb r0{{.*}}encoding: [0xea,0x01]
	sxu r0
; ENC: sxu r0{{.*}}encoding: [0xe8,0x02]
	sxl r0
; ENC: sxl r0{{.*}}encoding: [0xea,0x02]
	slz r0
; ENC: slz r0{{.*}}encoding: [0xe8,0x04]
	sly r0
; ENC: sly r0{{.*}}encoding: [0xe8,0x05]
	slx r0
; ENC: slx r0{{.*}}encoding: [0xe8,0x06]
	srz r0
; ENC: srz r0{{.*}}encoding: [0xea,0x08]
	sry r0
; ENC: sry r0{{.*}}encoding: [0xe8,0x07]
	srx r0
; ENC: srx r0{{.*}}encoding: [0xea,0x07]
	svz r0
; ENC: svz r0{{.*}}encoding: [0xea,0x04]
	svy r0
; ENC: svy r0{{.*}}encoding: [0xea,0x05]
	svx r0
; ENC: svx r0{{.*}}encoding: [0xea,0x06]
	zlb r7
; ENC: zlb r7{{.*}}encoding: [0xe9,0xc1]
	srz r7
; ENC: srz r7{{.*}}encoding: [0xeb,0xc8]
	SLX R5
; ENC: slx r5{{.*}}encoding: [0xe9,0x46]
	slz r1
; ENC: slz r1{{.*}}encoding: [0xe8,0x44]
	jys 1
; ENC: jys 1{{.*}}encoding: [0xe1,0x81]

; OBJ: Name: .text
; OBJ: Size: 42
; OBJ: SectionData (
; OBJ-NEXT: 0000: E803EA03 E808E801 EA01E802 EA02E804
; OBJ-NEXT: 0010: E805E806 EA08E807 EA07EA04 EA05EA06
; OBJ-NEXT: 0020: E9C1EBC8 E946E844 E181
; OBJ: Relocations [
; OBJ-NEXT: ]
