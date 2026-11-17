; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOC %s
; RUN: llc -mtriple=mera400-unknown-none < %s | %python %S/Inputs/as1-word-sim.py | FileCheck --check-prefix=SIM %s

; Every C data pointer is a byte pointer (address space 1). The machine reads
; and writes whole words, so a 16, 32 or 64-bit value or a double accessed
; through a byte pointer must be 2-aligned and is rewritten to a word access
; at (byte address >> 1): one shift ("srz") and then ordinary word loads and
; stores. Pointers made from the address of a global or a stack slot by
; "addrspacecast" are already known to be word addresses (the cast is "x << 1"),
; so the shift pair is not emitted at all: the access is direct.

@g = global i16 0
@arr = global [4 x i16] zeroinitializer
@gl = global i32 0
@gp = global ptr addrspace(1) addrspacecast (ptr @g to ptr addrspace(1))
@gp4 = global ptr addrspace(1) getelementptr (i8, ptr addrspace(1) addrspacecast (ptr @arr to ptr addrspace(1)), i16 4)

declare void @take(ptr addrspace(1))

; CHECK-LABEL: ld16:
; CHECK:      srz r1
; CHECK-NEXT: tw r1, r1
; CHECK-NEXT: uj r4
define i16 @ld16(ptr addrspace(1) %p) {
  %v = load i16, ptr addrspace(1) %p, align 2
  ret i16 %v
}

; The high word of a 32-bit value is at the lower address.
; CHECK-LABEL: st32:
; CHECK:      srz r1
; CHECK-NEXT: rw r3, 1+r1
; CHECK-NEXT: rw r2, r1
define void @st32(ptr addrspace(1) %p, i32 %x) {
  store i32 %x, ptr addrspace(1) %p, align 2
  ret void
}

; CHECK-LABEL: ld64:
; CHECK:      srz r2
; CHECK:      tw r3, 3+r2
; CHECK:      tw r2, r2
define i64 @ld64(ptr addrspace(1) %p) {
  %v = load i64, ptr addrspace(1) %p, align 2
  ret i64 %v
}

;; A double is four words in memory, three of them used.
; CHECK-LABEL: ldd:
; CHECK:      srz r1
; CHECK:      tw r5, r1
; CHECK-NEXT: tw r2, 1+r1
; CHECK-NEXT: tw r3, 2+r1
define double @ldd(ptr addrspace(1) %p) {
  %v = load double, ptr addrspace(1) %p, align 2
  ret double %v
}

; CHECK-LABEL: ldp:
; CHECK:      srz r1
; CHECK-NEXT: tw r1, r1
define ptr addrspace(1) @ldp(ptr addrspace(1) %p) {
  %v = load ptr addrspace(1), ptr addrspace(1) %p, align 2
  ret ptr addrspace(1) %v
}

;; Indexing a byte pointer by i16 elements scales the index by two (bytes), the
;; access then shifts the sum back.
; CHECK-LABEL: idx:
; CHECK:      slz r2
; CHECK-NEXT: aw r2, r1
; CHECK-NEXT: srz r2
; CHECK-NEXT: tw r1, r2
define i16 @idx(ptr addrspace(1) %p, i16 %i) {
  %q = getelementptr i16, ptr addrspace(1) %p, i16 %i
  %v = load i16, ptr addrspace(1) %q, align 2
  ret i16 %v
}

;; A GEP in address space 1 is plain byte arithmetic.
; CHECK-LABEL: byteoff:
; CHECK:      awt r1, 6
; CHECK-NEXT: srz r1
; CHECK-NEXT: tw r1, r1
define i16 @byteoff(ptr addrspace(1) %p) {
  %q = getelementptr i8, ptr addrspace(1) %p, i16 6
  %v = load i16, ptr addrspace(1) %q, align 2
  ret i16 %v
}

;; "int x; p = &x; *p = 7; return *p": no shift anywhere.
; CHECK-LABEL: local:
; CHECK-NOT:  s{{[rl]}}z
; CHECK:      lwt r1, 7
; CHECK-NEXT: rw r1, r7
; CHECK-NOT:  s{{[rl]}}z
define i16 @local() {
  %x = alloca i16
  %p = addrspacecast ptr %x to ptr addrspace(1)
  store i16 7, ptr addrspace(1) %p, align 2
  %v = load i16, ptr addrspace(1) %p, align 2
  ret i16 %v
}

; CHECK-LABEL: global_direct:
; CHECK-NEXT: {{^}}; %bb.0:
; CHECK-NEXT: tw r1, g
; CHECK-NEXT: uj r4
define i16 @global_direct() {
  %v = load i16, ptr addrspace(1) addrspacecast (ptr @g to ptr addrspace(1)), align 2
  ret i16 %v
}

;; Even byte offsets from a cast global are still direct word accesses.
; CHECK-LABEL: global_elem:
; CHECK-NEXT: {{^}}; %bb.0:
; CHECK-NEXT: tw r1, arr+2
define i16 @global_elem() {
  %p = getelementptr i16, ptr addrspace(1) addrspacecast (ptr @arr to ptr addrspace(1)), i16 2
  %v = load i16, ptr addrspace(1) %p, align 2
  ret i16 %v
}

;; The same through an instruction-level cast and GEP.
; CHECK-LABEL: global_elem_insn:
; CHECK-NEXT: {{^}}; %bb.0:
; CHECK-NEXT: tw r1, arr+3
define i16 @global_elem_insn() {
  %c = addrspacecast ptr @arr to ptr addrspace(1)
  %p = getelementptr i16, ptr addrspace(1) %c, i16 3
  %v = load i16, ptr addrspace(1) %p, align 2
  ret i16 %v
}

;; A byte access to an odd byte keeps the byte-pointer sequence.
; CHECK-LABEL: global_oddbyte:
; CHECK:      lw r2, arr
; CHECK-NEXT: slz r2
; CHECK-NEXT: lwt r1, 0
; CHECK-NEXT: lb r1, 3+r2
define i16 @global_oddbyte() {
  %p = getelementptr i8, ptr addrspace(1) addrspacecast (ptr @arr to ptr addrspace(1)), i16 3
  %v = load i8, ptr addrspace(1) %p
  %z = zext i8 %v to i16
  ret i16 %z
}

;; A 32-bit access to a cast global is direct as well.
; CHECK-LABEL: global_long:
; CHECK:      tw r1, gl
; CHECK-NEXT: tw r2, gl+1
define i32 @global_long() {
  %v = load i32, ptr addrspace(1) addrspacecast (ptr @gl to ptr addrspace(1)), align 2
  ret i32 %v
}

;; Taking the address of a global as a C pointer: the byte address.
; CHECK-LABEL: escape:
; CHECK:      lw r1, g
; CHECK-NEXT: slz r1
; CHECK-NEXT: rj r4, take
define void @escape() {
  call void @take(ptr addrspace(1) addrspacecast (ptr @g to ptr addrspace(1)))
  ret void
}

;; In a global initialiser the address is the symbol value, which is a byte
;; address: a 16-bit absolute relocation, not a word one.
; CHECK-LABEL: gp:
; CHECK-NEXT: .short g
; CHECK-LABEL: gp4:
; CHECK-NEXT: .short arr+4
; RELOC:      .rel.data
; RELOC:      R_MERA400_16 g
; RELOC:      R_MERA400_16 arr

; SIM: as1 word access ok
