; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

; Short displacements count words from the next instruction and objdump
; shows the byte address they reach; long jumps hold word addresses.
	.text
start:
	lwt r1, 3
loop:
	awt r1, -1
	irb r1, loop
	ujs fwd
	nop
fwd:
	uj 3
	uj [3]
	jes loop

; CHECK-LABEL: <start>:
; CHECK:      lwt r1, 3
; CHECK-LABEL: <loop>:
; CHECK-NEXT: awt r1, -1
; CHECK-NEXT: irb r1, -2 <loop>
; CHECK-NEXT: ujs 1 <fwd>
; CHECK-NEXT: nop
; CHECK-LABEL: <fwd>:
; CHECK-NEXT: uj 3 <loop+0x4>
; CHECK-NEXT: uj [3]
; CHECK-NEXT: jes -9 <loop>
