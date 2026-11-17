; REQUIRES: mera400
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large %s -o %t.s
; RUN: FileCheck %s --check-prefix=ASM < %t.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t.s -o %t.o
; RUN: ld.lld -Ttext=0x100 -e a %t.o -o %t.out
; RUN: llvm-objdump -s -j .rodata -j .data %t.out | FileCheck %s

;; In the large model, code pointers (function pointers, vtable slots) are
;; 32-bit BYTE addresses, the unit of an ELF symbol value, so "a" at 0x100 is
;; stored as 0x00000100: a plain R_MERA400_32 relocation, no shifting. The
;; functions are declared in address space 1 (the program address space of the
;; large data layout, which is what Clang produces for C++); the back end
;; moves them to address space 0, where code lives.
; ASM:      vt:
; ASM-NEXT:   .long a
; ASM-NEXT:   .long b
; ASM:      fp:
; ASM-NEXT:   .long b

; CHECK: Contents of section .rodata:
; CHECK-NEXT: 0108 00000100 00000104
; CHECK: Contents of section .data:
; CHECK-NEXT: 0110 00000104

target datalayout = "E-m:e-P1-G1-p:16:16-p1:32:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:0:16-n16-S16"

define i16 @a(ptr addrspace(1) %this) addrspace(1) {
  ret i16 1
}

define i16 @b(ptr addrspace(1) %this) addrspace(1) {
  ret i16 2
}

@vt = addrspace(1) constant [2 x ptr addrspace(1)] [ptr addrspace(1) @a, ptr addrspace(1) @b]
@fp = addrspace(1) global ptr addrspace(1) @b
