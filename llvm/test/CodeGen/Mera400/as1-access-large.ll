; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -enable-new-pm=0 < %s | FileCheck %s

; The large model: a byte pointer is 32 bits (high word in the first register),
; so a word access through it shifts the pair right by one (srz, sry) to get
; the word address, which is 16 bits. The result must lie in the first 64 K
; words; the high word's lowest bit is shifted out into the low word.

@g = global i16 0
@gp = global ptr addrspace(1) addrspacecast (ptr @g to ptr addrspace(1))

; CHECK-LABEL: ld16:
; CHECK:      srz r1
; CHECK-NEXT: sry r2
; CHECK-NEXT: tw r1, r2
define i16 @ld16(ptr addrspace(1) %p) {
  %v = load i16, ptr addrspace(1) %p, align 2
  ret i16 %v
}

; CHECK-LABEL: ld32:
; CHECK:      srz r1
; CHECK-NEXT: sry r2
; CHECK-NEXT: tw r1, r2
; CHECK-NEXT: tw r2, 1+r2
define i32 @ld32(ptr addrspace(1) %p) {
  %v = load i32, ptr addrspace(1) %p, align 2
  ret i32 %v
}

;; A pointer to a pointer: the loaded value is itself two words.
; CHECK-LABEL: ldp:
; CHECK:      srz r1
; CHECK-NEXT: sry r2
; CHECK-NEXT: tw r1, r2
; CHECK-NEXT: tw r2, 1+r2
define ptr addrspace(1) @ldp(ptr addrspace(1) %p) {
  %v = load ptr addrspace(1), ptr addrspace(1) %p, align 2
  ret ptr addrspace(1) %v
}

;; A cast global is a direct word access, no shifts.
; CHECK-LABEL: direct:
; CHECK-NEXT: {{^}}; %bb.0:
; CHECK-NEXT: tw r1, g
define i16 @direct() {
  %v = load i16, ptr addrspace(1) addrspacecast (ptr @g to ptr addrspace(1)), align 2
  ret i16 %v
}

;; The initialiser holds the 32-bit byte address of g: high word, then low.
; CHECK-LABEL: gp:
; CHECK:      .long g
