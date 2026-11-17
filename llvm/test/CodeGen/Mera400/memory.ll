; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llvm-objdump -dr %t.o | FileCheck --check-prefix=OBJ %s

; Word pointers (address space 0) hold word addresses and are offset in 16-bit
; words. Byte pointers (address space 1) hold byte addresses; "lb" keeps the
; high octet of its destination, so a byte load clears it first.

@g = external global i16
@arr = external global [10 x i16]
@ptr = external global ptr

; CHECK-LABEL: load_reg:
; CHECK:      tw r1, r1
; CHECK-NEXT: uj r4
define i16 @load_reg(ptr %p) {
  %v = load i16, ptr %p
  ret i16 %v
}

; CHECK-LABEL: store_reg:
; CHECK:      rw r2, r1
; CHECK-NEXT: uj r4
define void @store_reg(ptr %p, i16 %v) {
  store i16 %v, ptr %p
  ret void
}

; CHECK-LABEL: load_disp:
; CHECK:      tw r1, 5+r1
; CHECK-NEXT: uj r4
define i16 @load_disp(ptr %p) {
  %q = getelementptr i16, ptr %p, i16 5
  %v = load i16, ptr %q
  ret i16 %v
}

; CHECK-LABEL: store_disp:
; CHECK:      rw r2, -2+r1
; CHECK-NEXT: uj r4
define void @store_disp(ptr %p, i16 %v) {
  %q = getelementptr i16, ptr %p, i16 -2
  store i16 %v, ptr %q
  ret void
}

; CHECK-LABEL: load_index:
; CHECK:      aw r1, r2
; CHECK-NEXT: tw r1, r1
; CHECK-NEXT: uj r4
define i16 @load_index(ptr %p, i16 %i) {
  %q = getelementptr i16, ptr %p, i16 %i
  %v = load i16, ptr %q
  ret i16 %v
}

; CHECK-LABEL: load_global:
; CHECK:      tw r1, g
; CHECK-NEXT: uj r4
define i16 @load_global() {
  %v = load i16, ptr @g
  ret i16 %v
}

; CHECK-LABEL: store_global:
; CHECK:      rw r1, g
; CHECK-NEXT: uj r4
define void @store_global(i16 %v) {
  store i16 %v, ptr @g
  ret void
}

; CHECK-LABEL: load_global_elem:
; CHECK:      tw r1, arr+4
; CHECK-NEXT: uj r4
define i16 @load_global_elem() {
  %v = load i16, ptr getelementptr ([10 x i16], ptr @arr, i16 0, i16 4)
  ret i16 %v
}

; CHECK-LABEL: load_global_indexed:
; CHECK:      lw r2, arr
; CHECK-NEXT: aw r2, r1
; CHECK-NEXT: tw r1, r2
; CHECK-NEXT: uj r4
define i16 @load_global_indexed(i16 %i) {
  %q = getelementptr [10 x i16], ptr @arr, i16 0, i16 %i
  %v = load i16, ptr %q
  ret i16 %v
}

; CHECK-LABEL: load_ptr:
; CHECK:      tw r1, r1
; CHECK-NEXT: uj r4
define ptr @load_ptr(ptr %p) {
  %v = load ptr, ptr %p
  ret ptr %v
}

; CHECK-LABEL: addr_global:
; CHECK:      lw r1, arr
; CHECK-NEXT: uj r4
define ptr @addr_global() { ret ptr @arr }

; CHECK-LABEL: addr_global_elem:
; CHECK:      lw r1, arr
; CHECK-NEXT: awt r1, 3
; CHECK-NEXT: uj r4
define ptr @addr_global_elem() { ret ptr getelementptr ([10 x i16], ptr @arr, i16 0, i16 3) }

; CHECK-LABEL: load_byte:
; CHECK:      lwt r2, 0
; CHECK-NEXT: lb r2, r1
; CHECK-NEXT: lw r1, r2
; CHECK-NEXT: uj r4
define i16 @load_byte(ptr addrspace(1) %p) {
  %v = load i8, ptr addrspace(1) %p
  %z = zext i8 %v to i16
  ret i16 %z
}

; CHECK-LABEL: load_byte_disp:
; CHECK:      lwt r2, 0
; CHECK-NEXT: lb r2, 3+r1
; CHECK-NEXT: lw r1, r2
; CHECK-NEXT: uj r4
define i16 @load_byte_disp(ptr addrspace(1) %p) {
  %q = getelementptr i8, ptr addrspace(1) %p, i16 3
  %v = load i8, ptr addrspace(1) %q
  %z = zext i8 %v to i16
  ret i16 %z
}

; CHECK-LABEL: store_byte:
; CHECK:      rb r2, r1
; CHECK-NEXT: uj r4
define void @store_byte(ptr addrspace(1) %p, i16 %v) {
  %t = trunc i16 %v to i8
  store i8 %t, ptr addrspace(1) %p
  ret void
}

; CHECK-LABEL: copy_byte:
; CHECK:      lwt r3, 0
; CHECK-NEXT: lb r3, r2
; CHECK-NEXT: rb r3, r1
; CHECK-NEXT: uj r4
define void @copy_byte(ptr addrspace(1) %d, ptr addrspace(1) %s) {
  %v = load i8, ptr addrspace(1) %s
  store i8 %v, ptr addrspace(1) %d
  ret void
}

; CHECK-LABEL: byte_index:
; CHECK:      aw r1, r2
; CHECK-NEXT: rb r3, r1
; CHECK-NEXT: uj r4
define void @byte_index(ptr addrspace(1) %p, i16 %i, i16 %v) {
  %q = getelementptr i8, ptr addrspace(1) %p, i16 %i
  %t = trunc i16 %v to i8
  store i8 %t, ptr addrspace(1) %q
  ret void
}

; The assembled forms: the register form is one word, a displacement or a
; symbol needs an extra word, and a symbol gets a WORD16 relocation.
; OBJ:      tw r1, r1
; OBJ:      rw r2, r1
; OBJ:      tw r1, 5+r1
; OBJ:      rw r2, -2+r1
; OBJ:      tw r1, 0
; OBJ-NEXT: R_MERA400_WORD16 g
; OBJ:      rw r1, 0
; OBJ-NEXT: R_MERA400_WORD16 g
; OBJ:      tw r1, 4
; OBJ-NEXT: R_MERA400_WORD16 arr
; OBJ:      lwt r2, 0
; OBJ-NEXT: lb r2, r1
; OBJ:      lb r2, 3+r1
; OBJ:      rb r2, r1

