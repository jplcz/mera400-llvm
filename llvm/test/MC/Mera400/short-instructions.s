; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -show-encoding %s | FileCheck %s --check-prefix=ENC
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --file-headers --sections --section-data --symbols --relocations %t | FileCheck %s --check-prefix=OBJ

; LWT encodings are checked against the EMAS short_r acceptance fixture:
; https://github.com/jakubfi/emas/tree/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4/tests/acceptance/args

	.text
	.globl entry
entry:
	lwt r0, 0
; ENC: lwt r0, 0{{.*}}encoding: [0xd4,0x00]
	lwt r1, 1
; ENC: lwt r1, 1{{.*}}encoding: [0xd4,0x41]
	lwt r2, -1
; ENC: lwt r2, -1{{.*}}encoding: [0xd6,0x81]
	lwt r3, 63
; ENC: lwt r3, 63{{.*}}encoding: [0xd4,0xff]
	lwt r4, -63
; ENC: lwt r4, -63{{.*}}encoding: [0xd7,0x3f]
	awt r7, -63
; ENC: awt r7, -63{{.*}}encoding: [0xc3,0xff]
	cwt r0, 0
; ENC: cwt r0, 0{{.*}}encoding: [0xd0,0x00]
	AWT R5, (60 + 3)
; ENC: awt r5, 63{{.*}}encoding: [0xc1,0x7f]
	cwt r6, -1
; ENC: cwt r6, -1{{.*}}encoding: [0xd3,0x81]
end:
	.short end-entry

; OBJ: Format: elf32-mera400
; OBJ: Arch: mera400
; OBJ: Class: 32-bit
; OBJ: DataEncoding: BigEndian
; OBJ: Type: Relocatable
; OBJ: Machine: EM_MERA400_EXPERIMENTAL
; OBJ: Name: .text
; OBJ: Size: 20
; OBJ: SectionData (
; OBJ-NEXT: 0000: D400D441 D681D4FF D73FC3FF D000C17F
; OBJ-NEXT: 0010: D3810012
; OBJ: Relocations [
; OBJ-NEXT: ]
; OBJ: Name: end
; OBJ-NEXT: Value: 0x12
; OBJ: Name: entry
; OBJ-NEXT: Value: 0x0
