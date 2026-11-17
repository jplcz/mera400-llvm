; RUN: not llc -mtriple=mera400-unknown-none -mattr=+large-memory < %s 2>&1 | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -mattr=+large-memory -target-abi=large -filetype=null < %S/large-memory.ll
; RUN: llc -mtriple=mera400-unknown-none -mattr=-large-memory < %s | FileCheck --check-prefix=SMALL %s
; RUN: llc -mtriple=mera400-unknown-none -filetype=obj < %s -o %t.o
; RUN: llvm-readobj -h %t.o | FileCheck --check-prefix=OBJ %s
; OBJ: EF_MERA400_SMALL (0x2)

; The module data layout decides the memory model. The 'large-memory' feature
; is only checked against it: here the module has the small layout (p1:16:16).
; CHECK: the 'large-memory' feature requires a module data layout with 32-bit address space 1 pointers (p1:32:16)
; SMALL-NOT: .model
; SMALL: ld:
; SMALL: lb
target datalayout = "E-m:e-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:0:16-n16-S16"

define i8 @ld(ptr addrspace(1) %p) {
  %v = load i8, ptr addrspace(1) %p
  ret i8 %v
}
