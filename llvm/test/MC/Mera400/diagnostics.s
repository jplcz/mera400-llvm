; REQUIRES: mera400-registered-target
; RUN: not llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s
; RUN: printf '.quad local\nlocal: .byte 0\n' | not llvm-mc -triple=mera400-unknown-none -filetype=obj -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELOC

	lwt r1, 64
; CHECK: error: invalid operand for MERA-400 instruction
	lwt r1, -64
; CHECK: error: invalid operand for MERA-400 instruction
	lwt r8, 1
; CHECK: error: invalid operand for MERA-400 instruction
	lwt r1
; CHECK: error: invalid operand for MERA-400 instruction
	lws r1, 64
; CHECK: error: invalid operand for MERA-400 instruction
	lws r1, -64
; CHECK: error: invalid operand for MERA-400 instruction
	rws r1, 64
; CHECK: error: invalid operand for MERA-400 instruction
	rws r1, -64
; CHECK: error: invalid operand for MERA-400 instruction
	lws r8, 0
; CHECK: error: invalid operand for MERA-400 instruction
	trb r1, -64
; CHECK: error: invalid operand for MERA-400 instruction
	trb r1, 64
; CHECK: error: invalid operand for MERA-400 instruction
	irb r1, -64
; CHECK: error: invalid operand for MERA-400 instruction
	irb r1, 64
; CHECK: error: invalid operand for MERA-400 instruction
	drb r1, -64
; CHECK: error: invalid operand for MERA-400 instruction
	drb r1, 64
; CHECK: error: invalid operand for MERA-400 instruction
	trb r8, 0
; CHECK: error: invalid operand for MERA-400 instruction
	nga r8
; CHECK: error: invalid operand for MERA-400 instruction
	srz 1
; CHECK: error: invalid operand for MERA-400 instruction
	zlb
; CHECK: error: invalid operand for MERA-400 instruction
	sxu r1, r2
; CHECK: error: invalid operand for MERA-400 instruction
	ujs 64
; CHECK: error: invalid operand for MERA-400 instruction
	jes -64
; CHECK: error: invalid operand for MERA-400 instruction
	ujs
; CHECK: error: invalid operand for MERA-400 instruction
	nop 0
; CHECK: error: invalid operand for MERA-400 instruction
	unknown r1, 1
; CHECK: error: invalid or unsupported instruction mnemonic
	shc r1, 16
; CHECK: error: invalid operand for MERA-400 instruction
	hlt 128
; CHECK: error: invalid operand for MERA-400 instruction
	blc 1
; CHECK: error: invalid operand for MERA-400 instruction
	brc 256
; CHECK: error: invalid operand for MERA-400 instruction
	exl -1
; CHECK: error: invalid operand for MERA-400 instruction
	lw r1, 65536
; CHECK: error: invalid operand for MERA-400 instruction
	lw r1, -32769
; CHECK: error: invalid operand for MERA-400 instruction
	lw r1, [r2
; CHECK: error: expected ']'
; RELOC: error: unsupported MERA-400 relocation
