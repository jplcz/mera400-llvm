; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -show-encoding %s | FileCheck %s --check-prefix=ENC
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --sections --section-data %t | FileCheck %s --check-prefix=OBJ

; Expected words come from the EMAS acceptance fixtures (tests/acceptance/args/
; norm_*.out) at https://github.com/jakubfi/emas/tree/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4
; A normal argument is: indirect bit, register A, index register B, register C.
; Register C = 0 means that an extra 16-bit word follows.

; Immediate (extra word), norm_imm.
	lw r0, 0
; ENC: lw r0, 0{{.*}}encoding: [0x40,0x00,0x00,0x00]
	lw r1, 1
; ENC: lw r1, 1{{.*}}encoding: [0x40,0x40,0x00,0x01]
	lw r2, -1
; ENC: lw r2, -1{{.*}}encoding: [0x40,0x80,0xff,0xff]
	lw r3, -32768
; ENC: lw r3, -32768{{.*}}encoding: [0x40,0xc0,0x80,0x00]
	lw r4, 32767
; ENC: lw r4, 32767{{.*}}encoding: [0x41,0x00,0x7f,0xff]
	lw r5, 65535
; ENC: lw r5, -1{{.*}}encoding: [0x41,0x40,0xff,0xff]

; Immediate plus index register B, norm_imm_rB.
	lw r0, 0 + r7
; ENC: lw r0, 0+r7{{.*}}encoding: [0x40,0x38,0x00,0x00]
	lw r4, 32767 + r3
; ENC: lw r4, 32767+r3{{.*}}encoding: [0x41,0x18,0x7f,0xff]
	lw r2, -1 + r5
; ENC: lw r2, -1+r5{{.*}}encoding: [0x40,0xa8,0xff,0xff]

; Index register minus immediate, norm_rB_minus_imm.
	lw r1, r6 - 1
; ENC: lw r1, -1+r6{{.*}}encoding: [0x40,0x70,0xff,0xff]
	lw r2, r5 - -1
; ENC: lw r2, 1+r5{{.*}}encoding: [0x40,0xa8,0x00,0x01]
	lw r5, r2 - 32768
; ENC: lw r5, -32768+r2{{.*}}encoding: [0x41,0x50,0x80,0x00]

; Register C only and register C plus register B, norm_rC and norm_rC_rB.
	lw r0, r1
; ENC: lw r0, r1{{.*}}encoding: [0x40,0x01]
	lw r1, r0
; ENC: lw r1, r0{{.*}}encoding: [0x40,0x40]
	lw r0, r1 + r2
; ENC: lw r0, r1+r2{{.*}}encoding: [0x40,0x11]
	lw r0, r7 + r7
; ENC: lw r0, r7+r7{{.*}}encoding: [0x40,0x3f]

; Indirect forms, norm_Dmod.
	lw r1, [r2]
; ENC: lw r1, [r2]{{.*}}encoding: [0x42,0x42]
	lw r1, [r2+r3]
; ENC: lw r1, [r2+r3]{{.*}}encoding: [0x42,0x5a]
	lw r1, [3]
; ENC: lw r1, [3]{{.*}}encoding: [0x42,0x40,0x00,0x03]
	lw r1, [r4+1]
; ENC: lw r1, [1+r4]{{.*}}encoding: [0x42,0x60,0x00,0x01]
	lw r1, [1+r5]
; ENC: lw r1, [1+r5]{{.*}}encoding: [0x42,0x68,0x00,0x01]
	lw r1, [r6-1]
; ENC: lw r1, [-1+r6]{{.*}}encoding: [0x42,0x70,0xff,0xff]

; Instructions without a register operand, norm_no_r.
	uj r1
; ENC: uj r1{{.*}}encoding: [0xf0,0x01]
	uj r2+r3
; ENC: uj r2+r3{{.*}}encoding: [0xf0,0x1a]
	uj 3
; ENC: uj 3{{.*}}encoding: [0xf0,0x00,0x00,0x03]
	uj r4+3
; ENC: uj 3+r4{{.*}}encoding: [0xf0,0x20,0x00,0x03]
	uj 3+r5
; ENC: uj 3+r5{{.*}}encoding: [0xf0,0x28,0x00,0x03]
	uj r6-3
; ENC: uj -3+r6{{.*}}encoding: [0xf0,0x30,0xff,0xfd]
	uj [r1]
; ENC: uj [r1]{{.*}}encoding: [0xf2,0x01]
	uj [3+r5]
; ENC: uj [3+r5]{{.*}}encoding: [0xf2,0x28,0x00,0x03]

; Opcode and register-field samples from the EMAS instruction table.
	tw r1, 0
; ENC: tw r1, 0{{.*}}encoding: [0x44,0x40,0x00,0x00]
	cb r7, 0
; ENC: cb r7, 0{{.*}}encoding: [0xbd,0xc0,0x00,0x00]
	ad 0
; ENC: ad 0{{.*}}encoding: [0x7c,0x00,0x00,0x00]
	df 0
; ENC: df 0{{.*}}encoding: [0x7d,0xc0,0x00,0x00]
	lj 0
; ENC: lj 0{{.*}}encoding: [0xf1,0xc0,0x00,0x00]
	ib 0
; ENC: ib 0{{.*}}encoding: [0xfd,0xc0,0x00,0x00]

; Two-word instructions stay word aligned in objects.
; OBJ: SectionData (
; OBJ: 0000: 40000000 40400001
