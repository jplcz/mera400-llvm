; RUN: llc -mtriple=mera400-unknown-none -target-abi=large < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -enable-new-pm=0 < %s | FileCheck %s

; The large data layout makes address space 1 (32-bit byte addresses) the
; program address space (P1) and the globals address space (G1) so that function
; pointers and vtable slots are as wide as Clang's AST says. Code and the
; variables of the program really live in address space 0: the back end moves
; functions and AS1 globals there (Mera400LowerGlobalSpaces). A function
; pointer is the 32-bit byte address of the function; calling through it
; shifts it right by one (a word address is what rj needs).

; CHECK-LABEL: call_ptr:
; CHECK:      srz r1
; CHECK-NEXT: sry r2
; CHECK:      rj r4, r2
; CHECK-LABEL: addr_of:
; CHECK:      lw r2, callee
; CHECK:      slz r2
; CHECK-NEXT: sly r1
; CHECK-LABEL: tbl:
; CHECK-NEXT: .long callee
; CHECK-NEXT: .long call_ptr

define i16 @callee(i16 %x) addrspace(1) {
  ret i16 %x
}

define i16 @call_ptr(ptr addrspace(1) %f, i16 %v) addrspace(1) {
  %r = call i16 %f(i16 %v)
  ret i16 %r
}

define ptr addrspace(1) @addr_of() addrspace(1) {
  ret ptr addrspace(1) @callee
}

@tbl = addrspace(1) constant [2 x ptr addrspace(1)] [ptr addrspace(1) @callee, ptr addrspace(1) @call_ptr]
