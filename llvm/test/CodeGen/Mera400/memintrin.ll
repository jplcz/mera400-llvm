; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large < %s | FileCheck --check-prefix=LARGE %s
; RUN: llc -mtriple=mera400-unknown-none < %s | %python %S/Inputs/memintrin-sim.py | FileCheck --check-prefix=SIM %s

; llvm.memcpy, llvm.memmove and llvm.memset. A copy of a small constant number
; of bytes is expanded here, in the IR pass that knows about pointer units,
; into word (or, when the alignment is not known, octet) loads and stores; the
; generic expansion would add octet offsets to word pointers. Everything else
; is a call to the C library's memcpy, memmove or memset, whose pointers are
; byte addresses and whose size is a size_t (16 bits in the small model, 32
; in the large one): the word pointers of address space 0 are shifted left by
; one first. The library keeps the C argument order: r1 = destination,
; r2 = source or fill value, r3 = size.

declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1)
declare void @llvm.memcpy.inline.p0.p0.i16(ptr, ptr, i16, i1)
declare void @llvm.memmove.p0.p0.i16(ptr, ptr, i16, i1)
declare void @llvm.memset.p0.i16(ptr, i8, i16, i1)
declare void @llvm.memcpy.p1.p1.i16(ptr addrspace(1), ptr addrspace(1), i16, i1)
declare void @llvm.memcpy.p1.p0.i16(ptr addrspace(1), ptr, i16, i1)
declare void @llvm.memset.p1.i16(ptr addrspace(1), i8, i16, i1)

;; Word pointers are always word aligned: three word copies.
; CHECK-LABEL: cpy6:
; CHECK:      tw r3, r2
; CHECK-NEXT: rw r3, r1
; CHECK-NEXT: tw r3, 1+r2
; CHECK-NEXT: rw r3, 1+r1
; CHECK-NEXT: tw r2, 2+r2
; CHECK-NEXT: rw r2, 2+r1
; CHECK-NEXT: uj r4
define void @cpy6(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i16(ptr %d, ptr %s, i16 6, i1 false)
  ret void
}

;; Byte pointers with an unknown alignment: octet copies (lb, rb).
; CHECK-LABEL: cpy4_bytes:
; CHECK:      lb r3, r2
; CHECK-NEXT: rb r3, r1
; CHECK:      lb r3, 3+r2
; CHECK-NEXT: rb r3, 3+r1
define void @cpy4_bytes(ptr addrspace(1) %d, ptr addrspace(1) %s) {
  call void @llvm.memcpy.p1.p1.i16(ptr addrspace(1) %d, ptr addrspace(1) %s, i16 4, i1 false)
  ret void
}

;; Aligned byte pointers: word copies, and an odd tail as one octet.
; CHECK-LABEL: cpy5_aligned:
; CHECK:      srz
; CHECK:      lb r3, 4+r2
; CHECK-NEXT: rb r3, 4+r1
define void @cpy5_aligned(ptr addrspace(1) %d, ptr addrspace(1) %s) {
  call void @llvm.memcpy.p1.p1.i16(ptr addrspace(1) align 2 %d, ptr addrspace(1) align 2 %s, i16 5, i1 false)
  ret void
}

;; Too long to expand (20 words): the library call, size in r3.
; CHECK-LABEL: cpy40_call:
; CHECK:      lwt r3, 40
; CHECK-NEXT: rj r4, memcpy
; LARGE-LABEL: cpy40_call:
; LARGE:      lwt r3, 40
; LARGE:      rj r4, memcpy
define void @cpy40_call(ptr addrspace(1) %d, ptr addrspace(1) %s) {
  call void @llvm.memcpy.p1.p1.i16(ptr addrspace(1) align 2 %d, ptr addrspace(1) align 2 %s, i16 40, i1 false)
  ret void
}

;; llvm.memcpy.inline must never become a call, however long.
; CHECK-LABEL: inline40:
; CHECK-NOT:  rj
; CHECK:      tw r2, 19+r2
; CHECK-NEXT: rw r2, 19+r1
define void @inline40(ptr %d, ptr %s) {
  call void @llvm.memcpy.inline.p0.p0.i16(ptr %d, ptr %s, i16 40, i1 false)
  ret void
}

; CHECK-LABEL: volatile_copy:
; CHECK:      tw r2, r2
; CHECK-NEXT: rw r2, r1
define void @volatile_copy(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i16(ptr %d, ptr %s, i16 2, i1 true)
  ret void
}

;; The source may overlap the destination, so everything is read first.
; CHECK-LABEL: move4:
; CHECK:      tw r3, 1+r2
; CHECK-NEXT: tw r2, r2
; CHECK-NEXT: rw r2, r1
; CHECK-NEXT: rw r3, 1+r1
define void @move4(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i16(ptr %d, ptr %s, i16 4, i1 false)
  ret void
}

;; Word pointers become byte pointers (slz) for the library.
; CHECK-LABEL: move_call:
; CHECK:      slz r1
; CHECK-NEXT: slz r2
; CHECK-NEXT: rj r4, memmove
; LARGE-LABEL: move_call:
; LARGE:      slz r2
; LARGE:      sly r6
; LARGE:      rj r4, memmove
define void @move_call(ptr %d, ptr %s, i16 %n) {
  call void @llvm.memmove.p0.p0.i16(ptr %d, ptr %s, i16 %n, i1 false)
  ret void
}

;; The fill value is repeated in both octets of a word.
; CHECK-LABEL: set6:
; CHECK:      lw r2, 16705
; CHECK-NEXT: rw r2, r1
; CHECK-NEXT: rw r2, 1+r1
; CHECK-NEXT: rw r2, 2+r1
define void @set6(ptr %d) {
  call void @llvm.memset.p0.i16(ptr %d, i8 65, i16 6, i1 false)
  ret void
}

;; A variable fill value and an odd size: words plus one octet (rb).
; CHECK-LABEL: set5:
; CHECK:      shc r3, 8
; CHECK:      rb r2, 4+r1
define void @set5(ptr %d, i8 %c) {
  call void @llvm.memset.p0.i16(ptr %d, i8 %c, i16 5, i1 false)
  ret void
}

;; The C prototype takes the value as an int: the octet is masked.
; CHECK-LABEL: set_call:
; CHECK:      nr r2, 255
; CHECK-NEXT: rj r4, memset
; LARGE-LABEL: set_call:
; LARGE:      nr r3, 255
; LARGE:      rj r4, memset
define void @set_call(ptr addrspace(1) %d, i8 %c, i16 %n) {
  call void @llvm.memset.p1.i16(ptr addrspace(1) %d, i8 %c, i16 %n, i1 false)
  ret void
}

;; Mixed pointer kinds: the word pointer is shifted to a byte pointer.
; CHECK-LABEL: mixed:
; CHECK:      slz r2
; CHECK:      lb r3, r2
define void @mixed(ptr addrspace(1) %d, ptr %s) {
  call void @llvm.memcpy.p1.p0.i16(ptr addrspace(1) %d, ptr %s, i16 4, i1 false)
  ret void
}

;; Between two stack objects: direct word accesses, no shifts.
; CHECK-LABEL: stack_copy:
; CHECK-NOT:  s{{[rl]}}z
; CHECK:      tw r1, r7
; CHECK-NEXT: rw r1, 4+r7
define void @stack_copy() {
  %a = alloca [4 x i16]
  %b = alloca [4 x i16]
  call void @llvm.memcpy.p0.p0.i16(ptr %a, ptr %b, i16 8, i1 false)
  ret void
}

; SIM: memory intrinsics ok
