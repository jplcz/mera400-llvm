; RUN: not llc -mtriple=mera400-unknown-none %s -o /dev/null 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mera400-unknown-none -enable-new-pm=0 %s -o /dev/null 2>&1 | FileCheck %s

; Everything the backend cannot implement yet is reported as an error that
; names the function; nothing is silently miscompiled. Structure passing,
; integers wider than 64 bits, tail calls and variable-sized allocas
; are reported by the lowering;
; memory accesses of the wrong width, unit or alignment and atomic memory
; operations by an IR pass that runs first.

declare void @byval(ptr byval(i16))
declare void @huge_arg(i128)
declare void @take(ptr)
declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1)
@byte_global = external addrspace(1) global i8

; CHECK-DAG: in function byval_call {{.*}}: Mera400: this kind of argument{{.*}}is not supported
define void @byval_call(ptr %p) {
  call void @byval(ptr byval(i16) %p)
  ret void
}

; CHECK-DAG: in function huge_call {{.*}}: Mera400: integers wider than 64 bits are not supported
define void @huge_call() {
  call void @huge_arg(i128 5)
  ret void
}

; CHECK-DAG: in function huge {{.*}}: Mera400: integers wider than 64 bits are not supported
define i128 @huge(i128 %a) {
  ret i128 %a
}

; CHECK-DAG: in function huge_load {{.*}}: Mera400: integers wider than 64 bits are not supported
define i128 @huge_load(ptr %p) {
  %v = load i128, ptr %p
  ret i128 %v
}

; CHECK-DAG: in function misaligned_word {{.*}}: Mera400: misaligned access
define i16 @misaligned_word(ptr addrspace(1) %p) {
  %v = load i16, ptr addrspace(1) %p, align 1
  ret i16 %v
}

; CHECK-DAG: in function misaligned_long {{.*}}: Mera400: misaligned access
define void @misaligned_long(ptr addrspace(1) %p) {
  store i32 0, ptr addrspace(1) %p, align 1
  ret void
}

; CHECK-DAG: in function odd_sized_byte_pointer_access {{.*}}: Mera400: only 8, 16, 32 and 64-bit integers, doubles and pointers can be accessed through a byte pointer
define i24 @odd_sized_byte_pointer_access(ptr addrspace(1) %p) {
  %v = load i24, ptr addrspace(1) %p, align 2
  ret i24 %v
}

; CHECK-DAG: in function atomic {{.*}}: Mera400: atomic operations are not supported
define i16 @atomic(ptr %p) {
  %v = load atomic i16, ptr %p seq_cst, align 2
  ret i16 %v
}

; CHECK-DAG: in function other_address_space {{.*}}: Mera400: address space 2 is not supported
define i16 @other_address_space(ptr addrspace(2) %p) {
  %v = load i16, ptr addrspace(2) %p
  ret i16 %v
}

; CHECK-DAG: in function global_in_byte_space {{.*}}: Mera400: global variables must be in address space 0
define i16 @global_in_byte_space() {
  %v = load i8, ptr addrspace(1) @byte_global
  %z = zext i8 %v to i16
  ret i16 %z
}

; CHECK-DAG: in function atomic_copy {{.*}}: Mera400: atomic memory copy and fill operations are not supported
define void @atomic_copy(ptr %d, ptr %s) {
  call void @llvm.memcpy.element.unordered.atomic.p0.p0.i16(ptr align 2 %d, ptr align 2 %s, i16 4, i32 2)
  ret void
}
declare void @llvm.memcpy.element.unordered.atomic.p0.p0.i16(ptr, ptr, i16, i32 immarg)

; CHECK-DAG: in function variable_alloca {{.*}}: Mera400: variable-sized stack allocation (alloca) is not supported
define void @variable_alloca(i16 %n) {
  %p = alloca i16, i16 %n
  call void @take(ptr %p)
  ret void
}

; CHECK-DAG: in function over_aligned {{.*}}: Mera400: stack objects aligned to more than one word are not supported
define void @over_aligned() {
  %p = alloca i16, align 8
  call void @take(ptr %p)
  ret void
}

; CHECK-DAG: in function tail {{.*}}: Mera400: tail calls are not supported
define i16 @tail(i16 %a) {
  %r = musttail call i16 @tail(i16 %a)
  ret i16 %r
}
