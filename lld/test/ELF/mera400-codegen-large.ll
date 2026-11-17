; REQUIRES: mera400
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large %s -o %t.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t.s -o %t.o
; RUN: ld.lld -Ttext=0x100 -e first %t.o -o %t.out
; RUN: llvm-readobj -h %t.out | FileCheck %s --check-prefix=FLAGS
; RUN: llvm-objdump -s -j .data %t.out | FileCheck %s

;; A large-model byte pointer in data is a 32-bit linear byte address
;; (R_MERA400_32). "str" is at byte 0x11a and "p" points at its second byte.
; FLAGS: EF_MERA400_LARGE (0x1)
; CHECK: Contents of section .data:
; CHECK-NEXT: 011a 00010203 0000011b

target datalayout = "E-m:e-p:16:16-p1:32:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:0:16-n16-S16"

@str = global [4 x i8] [i8 0, i8 1, i8 2, i8 3]
@p = global ptr addrspace(1) getelementptr (i8, ptr addrspace(1) addrspacecast (ptr @str to ptr addrspace(1)), i32 1)

define i8 @first(ptr addrspace(1) %q) {
  %v = load i8, ptr addrspace(1) %q
  ret i8 %v
}
