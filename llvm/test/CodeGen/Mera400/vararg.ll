; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llc -mtriple=mera400-unknown-none < %s | %python %S/Inputs/vararg-sim.py small | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large < %s | %python %S/Inputs/vararg-sim.py large | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -enable-new-pm=0 < %s | FileCheck --check-prefix=LARGE %s

; Variable arguments. The named arguments are passed as usual (R1 to R3, then
; the stack). Every variadic argument goes on the stack, one word per slot
; starting at the first free slot, even when registers are free, so that
; "va_arg" can walk one memory area: an i16 or a pointer takes one word, an
; i32 two and an i64 four (high word first) and a double three (the padding
; word of its memory format is not passed). Slot k of the stack arguments is
; at k + R7 at the call and at k + (R7 on entry) in the callee.
;
; A va_list is a byte pointer (address space 1) to the next slot: "va_start"
; stores the byte address of the first variadic slot, and "va_arg" loads the
; value there and moves the pointer on by the size of the slot (a byte
; pointer, so 2 per word). Clang expands "va_arg" itself; the back end only
; needs "va_start", "va_copy" and "va_end".

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_start.p1(ptr addrspace(1))
declare void @llvm.va_end.p0(ptr)
declare void @llvm.va_copy.p0(ptr, ptr)
@gd = global double 0.0
declare void @take(i16, ...)
declare void @take2(i16, i16, i16, i16, ...)

;; i16 2 in slot 0, i32 3 in slots 1-2, the double 1.0 in slots 3-5; only the
;; named argument is in a register.
; CHECK-LABEL: caller:
; CHECK:      lw r1, 16384
; CHECK-NEXT: rw r1, 3+r7
; CHECK-NEXT: lwt r1, 3
; CHECK-NEXT: rw r1, 2+r7
; CHECK-NEXT: lwt r1, 0
; CHECK-NEXT: rw r1, 4+r7
; CHECK-NEXT: rw r1, 1+r7
; CHECK-NEXT: lwt r1, 2
; CHECK-NEXT: rw r1, r7
; CHECK-NEXT: lwt r1, 1
; CHECK-NEXT: rw r1, 5+r7
; CHECK-NEXT: rj r4, take
define void @caller() {
  call void (i16, ...) @take(i16 1, i16 2, i32 3, double 1.0)
  ret void
}

;; The fourth named argument is already on the stack; the variadic ones follow
;; it in slots 1 onwards.
; CHECK-LABEL: caller_named:
; CHECK:      rw r2, 5+r7
; CHECK:      lwt r2, 5
; CHECK-NEXT: rw r2, 1+r7
; CHECK-NEXT: lwt r2, 4
; CHECK-NEXT: rw r2, r7
; CHECK-NEXT: lwt r2, 2
; CHECK-NEXT: lwt r3, 3
; CHECK-NEXT: rj r4, take2
define void @caller_named(i16 %x) {
  call void (i16, i16, i16, i16, ...) @take2(i16 %x, i16 2, i16 3, i16 4, i16 5, i64 6)
  ret void
}

;; No variadic argument: nothing on the stack.
; CHECK-LABEL: caller_none:
; CHECK:      lwt r1, 1
; CHECK-NEXT: rj r4, take
define void @caller_none() {
  call void (i16, ...) @take(i16 1)
  ret void
}

;; The va_list is the byte address of the first slot: (R7 + frame size + 0) * 2.
; CHECK-LABEL: start:
; CHECK:      lw r1, r7
; CHECK-NEXT: awt r1, 1
; CHECK-NEXT: slz r1
; CHECK-NEXT: rw r1, r7
define ptr addrspace(1) @start(i16 %n, ...) {
  %ap = alloca ptr addrspace(1)
  call void @llvm.va_start.p0(ptr %ap)
  %v = load ptr addrspace(1), ptr %ap
  ret ptr addrspace(1) %v
}

;; Five named arguments: r1 to r3 and two stack slots, so the list starts at
;; slot 2.
; CHECK-LABEL: start_named:
; CHECK:      lw r1, r7
; CHECK-NEXT: awt r1, 3
; CHECK-NEXT: slz r1
define ptr addrspace(1) @start_named(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e, ...) {
  %ap = alloca ptr addrspace(1)
  call void @llvm.va_start.p0(ptr %ap)
  %v = load ptr addrspace(1), ptr %ap
  ret ptr addrspace(1) %v
}

;; Each va_arg: load the list, load the word at ptr >> 1, store ptr + 2.
; CHECK-LABEL: sum3:
; CHECK:      slz r2
; CHECK:      srz r1
; CHECK-NEXT: tw r3, r1
; CHECK:      awt r1, 2
; CHECK:      awt r5, 4
; CHECK:      awt r2, 6
define i16 @sum3(i16 %n, ...) {
  %ap = alloca ptr addrspace(1)
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, i16
  %b = va_arg ptr %ap, i16
  %c = va_arg ptr %ap, i16
  call void @llvm.va_end.p0(ptr %ap)
  %s = add i16 %a, %b
  %t = add i16 %s, %c
  ret i16 %t
}

; CHECK-LABEL: long2:
; CHECK:      awt r5, 4
; CHECK:      awt r3, 8
define i32 @long2(i16 %n, ...) {
  %ap = alloca ptr addrspace(1)
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, i32
  %b = va_arg ptr %ap, i32
  %s = add i32 %a, %b
  ret i32 %s
}

;; A double is three words (a bump of 6 bytes): the fourth word is not read and
;; the padding word of the double in memory is written as zero.
; CHECK-LABEL: dbl:
; CHECK:      awt r2, 4
; CHECK:      rw r5, gd
; CHECK:      rw r3, gd+1
; CHECK:      rw r2, gd+2
; CHECK-NEXT: lwt r2, 0
; CHECK-NEXT: rw r2, gd+3
; CHECK:      awt r1, 8
define void @dbl(i16 %n, ...) {
  %ap = alloca ptr addrspace(1)
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, double
  %b = va_arg ptr %ap, i16
  store double %a, ptr @gd
  ret void
}

;; va_copy copies the pointer: advancing one list does not move the other.
; CHECK-LABEL: sec:
; CHECK-NOT:  rj
define i16 @sec(i16 %n, ...) {
  %ap = alloca ptr addrspace(1)
  %cp = alloca ptr addrspace(1)
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, i16
  call void @llvm.va_copy.p0(ptr %cp, ptr %ap)
  %b = va_arg ptr %ap, i16
  %c = va_arg ptr %cp, i16
  call void @llvm.va_end.p0(ptr %cp)
  %s = add i16 %b, %c
  ret i16 %s
}

;; A va_list that is itself reached through a byte pointer.
; CHECK-LABEL: as1_list:
; CHECK:      slz r1
; CHECK:      awt r2, 2
define i16 @as1_list(i16 %n, ...) {
  %o = alloca ptr addrspace(1)
  %ap = addrspacecast ptr %o to ptr addrspace(1)
  call void @llvm.va_start.p1(ptr addrspace(1) %ap)
  %v = va_arg ptr addrspace(1) %ap, i16
  ret i16 %v
}

;; A value narrower than a word still takes a whole slot.
; CHECK-LABEL: bytearg:
; CHECK:      awt r2, 2
define i8 @bytearg(i16 %n, ...) {
  %ap = alloca ptr addrspace(1)
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, i8
  ret i8 %a
}

;; Large model: the va_list is a 32-bit byte address, two words with the high
;; one first. Its high word is bit 15 of the word address.
; LARGE-LABEL: start:
; LARGE:      shc r{{[0-9]}}, 15
; LARGE-NEXT: nr r{{[0-9]}}, 1
; LARGE:      slz r{{[0-9]}}

; SIM: varargs ok
