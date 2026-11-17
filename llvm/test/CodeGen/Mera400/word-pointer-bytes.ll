; RUN: llc -mtriple=mera400-unknown-none < %s | FileCheck %s

; Clang reaches single bytes of a stack slot or global through the plain word
; pointer (address space 0) it created, so a byte access or an odd byte offset
; is rewritten to go through a byte pointer instead of being rejected.

@g = global [4 x i16] zeroinitializer

; CHECK-LABEL: byte_of_slot:
; CHECK: rb
define void @byte_of_slot() {
  %s = alloca [4 x i8], align 2
  %p = getelementptr i8, ptr %s, i16 3
  store i8 7, ptr %p
  %q = getelementptr i8, ptr %s, i16 1
  %v = load i8, ptr %q
  store volatile i8 %v, ptr @g
  ret void
}

; CHECK-LABEL: odd_memset:
define void @odd_memset() {
  %s = alloca [4 x i8], align 2
  %p = getelementptr i8, ptr %s, i16 3
  call void @llvm.memset.p0.i16(ptr %p, i8 0, i16 1, i1 false)
  ret void
}

; A byte-scaled variable index on a word pointer (C++ ABI vtable offsets).
; CHECK-LABEL: variable_byte_index:
define i16 @variable_byte_index(ptr %p, i16 %n) {
  %q = getelementptr i8, ptr %p, i16 %n
  %v = load i16, ptr %q
  ret i16 %v
}

declare void @llvm.memset.p0.i16(ptr, i8, i16, i1)
