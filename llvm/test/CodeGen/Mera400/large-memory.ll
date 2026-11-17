; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -verify-machineinstrs < %s | FileCheck --check-prefixes=CHECK,DATA %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -mattr=+large-memory < %s | FileCheck --check-prefixes=CHECK,DATA %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large < %s | %python %S/Inputs/large-bytes-sim.py | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llvm-readobj -h %t.o | FileCheck --check-prefix=FLAGS %s
; RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOC %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -filetype=obj < %s -o %t2.o
; RUN: llvm-readobj -h %t2.o | FileCheck --check-prefix=FLAGS %s

; The large model: a pointer in address space 1 is a 32-bit linear byte address
; (two registers, high word first). The machine can only access whole words, so
; a byte access reads (and for a store, writes back) the word at B >> 1 and
; picks the octet with B & 1; the even byte is the high octet.

target datalayout = "E-m:e-p:16:16-p1:32:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:0:16-n16-S16"

; CHECK: .model large

@g = global [10 x i16] zeroinitializer
@pg = global ptr addrspace(1) addrspacecast (ptr getelementptr ([10 x i16], ptr @g, i16 0, i16 3) to ptr addrspace(1))
@pn = global ptr addrspace(1) null

; CHECK-LABEL: ld:
; CHECK: srz r1
; CHECK: sry
; CHECK: tw r1,
; CHECK: nr r2, 1
; CHECK: shc r1, 8
; CHECK: nr r1, 255
define i8 @ld(ptr addrspace(1) %p) {
  %v = load i8, ptr addrspace(1) %p
  ret i8 %v
}

; CHECK-LABEL: st:
; CHECK: srz r1
; CHECK: sry
; CHECK: nr r2, 1
; CHECK: nr {{r[0-9]}}, -256
; CHECK: shc {{r[0-9]}}, 8
; CHECK: rw {{r[0-9]}}, r5
define void @st(ptr addrspace(1) %p, i8 %v) {
  store i8 %v, ptr addrspace(1) %p
  ret void
}

; A word pointer is widened and doubled, a byte pointer is halved and narrowed.
; CHECK-LABEL: cast:
; CHECK: lwt r1, 0
; CHECK: slz
; CHECK: sly r1
define ptr addrspace(1) @cast(ptr %p) {
  %q = addrspacecast ptr %p to ptr addrspace(1)
  ret ptr addrspace(1) %q
}

; CHECK-LABEL: back:
; CHECK: srz r1
; CHECK: sry r2
; CHECK-NEXT: lw r1, r2
define ptr @back(ptr addrspace(1) %p) {
  %q = addrspacecast ptr addrspace(1) %p to ptr
  ret ptr %q
}

; The address of a global in code is its word address, doubled.
; CHECK-LABEL: gaddr:
; CHECK: lw r2, g
; CHECK: awt r2, 3
; CHECK: slz r2
; CHECK: sly r1
define ptr addrspace(1) @gaddr() {
  ret ptr addrspace(1) addrspacecast (ptr getelementptr ([10 x i16], ptr @g, i16 0, i16 3) to ptr addrspace(1))
}

; Pointer arithmetic and comparison are plain 32-bit integer operations.
; CHECK-LABEL: diff:
; CHECK: sw r1, r2
define i16 @diff(ptr addrspace(1) %a, ptr addrspace(1) %b) {
  %d = ptrtoint ptr addrspace(1) %a to i32
  %e = ptrtoint ptr addrspace(1) %b to i32
  %s = sub i32 %d, %e
  %t = trunc i32 %s to i16
  ret i16 %t
}

; CHECK-LABEL: next:
; CHECK: aw r2, r3
; CHECK: ac r1, r5
define ptr addrspace(1) @next(ptr addrspace(1) %p) {
  %q = getelementptr i8, ptr addrspace(1) %p, i32 1
  ret ptr addrspace(1) %q
}

; CHECK-LABEL: less:
; CHECK: cl r3, r5
; CHECK: jls
define i16 @less(ptr addrspace(1) %a, ptr addrspace(1) %b) {
  %c = icmp ult ptr addrspace(1) %a, %b
  %r = zext i1 %c to i16
  ret i16 %r
}

; The initializer is the byte address (g + 6), a 32-bit relocation.
; DATA-LABEL: pg:
; DATA-NEXT: .long g+6
; DATA-LABEL: pn:
; DATA-NEXT: .long 0

; FLAGS: EF_MERA400_LARGE (0x1)
; The simulation of the ld and st sequences (both octets, word addresses
; below, at and above 0x8000) is in Inputs/large-bytes-sim.py.
; SIM: large byte access: 72 checks passed
; RELOC: R_MERA400_32 g
