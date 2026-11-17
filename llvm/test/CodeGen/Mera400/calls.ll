; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llvm-objdump -dr %t.o | FileCheck --check-prefix=OBJ %s

; Calling convention: the first three 16-bit arguments are in R1-R3, the rest
; are in words on the stack, argument 4 at the stack pointer (R7) at the time
; of the call, argument 5 at R7+1 and so on. The result is in R1. "rj r4, f"
; stores the return address in R4 and jumps to f; the caller removes nothing
; from the stack, because the outgoing arguments live in its own frame.

@fp = external global ptr
declare i16 @ext(i16)
declare void @void_ext()
declare i16 @args5(i16, i16, i16, i16, i16)
declare i16 @args7(i16, i16, i16, i16, i16, i16, i16)

; CHECK-LABEL: call_void:
; CHECK:      awt r7, -1
; CHECK-NEXT: rw r4, r7
; CHECK-NEXT: rj r4, void_ext
; CHECK-NEXT: tw r4, r7
; CHECK-NEXT: awt r7, 1
; CHECK-NEXT: uj r4
define void @call_void() {
  call void @void_ext()
  ret void
}

; CHECK-LABEL: call_result:
; CHECK:      awt r7, -1
; CHECK-NEXT: rw r4, r7
; CHECK-NEXT: rj r4, ext
; CHECK-NEXT: tw r4, r7
; CHECK-NEXT: awt r7, 1
; CHECK-NEXT: uj r4
define i16 @call_result(i16 %a) {
  %r = call i16 @ext(i16 %a)
  ret i16 %r
}

; CHECK-LABEL: three_regs:
; CHECK:      sw r1, r2
; CHECK-NEXT: sw r1, r3
; CHECK-NEXT: uj r4
define i16 @three_regs(i16 %a, i16 %b, i16 %c) {
  %x = sub i16 %a, %b
  %y = sub i16 %x, %c
  ret i16 %y
}

; CHECK-LABEL: stack_args_callee:
; CHECK:      tw r2, r7
; CHECK-NEXT: aw r1, r2
; CHECK-NEXT: tw r2, 1+r7
; CHECK-NEXT: aw r1, r2
; CHECK-NEXT: uj r4
define i16 @stack_args_callee(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e) {
  %x = add i16 %a, %d
  %y = add i16 %x, %e
  ret i16 %y
}

; CHECK-LABEL: stack_args_caller:
; CHECK:      awt r7, -3
; CHECK-NEXT: rw r4, 2+r7
; CHECK-NEXT: lwt r3, 5
; CHECK-NEXT: rw r3, 1+r7
; CHECK-NEXT: lwt r3, 4
; CHECK-NEXT: rw r3, r7
; CHECK-NEXT: lwt r3, 3
; CHECK-NEXT: rj r4, args5
; CHECK-NEXT: tw r4, 2+r7
; CHECK-NEXT: awt r7, 3
; CHECK-NEXT: uj r4
define i16 @stack_args_caller(i16 %a, i16 %b) {
  %r = call i16 @args5(i16 %a, i16 %b, i16 3, i16 4, i16 5)
  ret i16 %r
}

; CHECK-LABEL: forward_stack_args:
; CHECK:      awt r7, -3
; CHECK-NEXT: rw r4, 2+r7
; CHECK-NEXT: rw r1, 1+r7
; CHECK-NEXT: rw r2, r7
; CHECK-NEXT: tw r1, 4+r7
; CHECK-NEXT: tw r2, 3+r7
; CHECK-NEXT: rj r4, args5
; CHECK-NEXT: tw r4, 2+r7
; CHECK-NEXT: awt r7, 3
; CHECK-NEXT: uj r4
define i16 @forward_stack_args(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e) {
  %r = call i16 @args5(i16 %e, i16 %d, i16 %c, i16 %b, i16 %a)
  ret i16 %r
}

; CHECK-LABEL: seven_args:
; CHECK:      awt r7, -5
; CHECK-NEXT: rw r4, 4+r7
; CHECK-NEXT: lwt r1, 7
; CHECK-NEXT: rw r1, 3+r7
; CHECK-NEXT: lwt r1, 6
; CHECK-NEXT: rw r1, 2+r7
; CHECK-NEXT: lwt r1, 5
; CHECK-NEXT: rw r1, 1+r7
; CHECK-NEXT: lwt r1, 4
; CHECK-NEXT: rw r1, r7
; CHECK-NEXT: lwt r1, 1
; CHECK-NEXT: lwt r2, 2
; CHECK-NEXT: lwt r3, 3
; CHECK-NEXT: rj r4, args7
; CHECK-NEXT: tw r4, 4+r7
; CHECK-NEXT: awt r7, 5
; CHECK-NEXT: uj r4
define i16 @seven_args() {
  %r = call i16 @args7(i16 1, i16 2, i16 3, i16 4, i16 5, i16 6, i16 7)
  ret i16 %r
}

; CHECK-LABEL: indirect:
; CHECK:      awt r7, -1
; CHECK-NEXT: rw r4, r7
; CHECK-NEXT: lw r3, r1
; CHECK-NEXT: lw r1, r2
; CHECK-NEXT: rj r4, r3
; CHECK-NEXT: tw r4, r7
; CHECK-NEXT: awt r7, 1
; CHECK-NEXT: uj r4
define i16 @indirect(ptr %f, i16 %a) {
  %r = call i16 %f(i16 %a)
  ret i16 %r
}

; CHECK-LABEL: indirect_global:
; CHECK:      awt r7, -1
; CHECK-NEXT: rw r4, r7
; CHECK-NEXT: tw r2, fp
; CHECK-NEXT: rj r4, r2
; CHECK-NEXT: tw r4, r7
; CHECK-NEXT: awt r7, 1
; CHECK-NEXT: uj r4
define i16 @indirect_global(i16 %a) {
  %f = load ptr, ptr @fp
  %r = call i16 %f(i16 %a)
  ret i16 %r
}

; CHECK-LABEL: function_address:
; CHECK:      lw r1, ext
; CHECK-NEXT: uj r4
define ptr @function_address() {
  ret ptr @ext
}

; CHECK-LABEL: call_twice:
; CHECK:      awt r7, -3
; CHECK-NEXT: rw r4, 2+r7
; CHECK-NEXT: rw r5, 1+r7
; CHECK-NEXT: rw r6, r7
; CHECK-NEXT: lw r5, r2
; CHECK-NEXT: rj r4, ext
; CHECK-NEXT: lw r6, r1
; CHECK-NEXT: lw r1, r5
; CHECK-NEXT: rj r4, ext
; CHECK-NEXT: aw r1, r6
; CHECK-NEXT: tw r6, r7
; CHECK-NEXT: tw r5, 1+r7
; CHECK-NEXT: tw r4, 2+r7
; CHECK-NEXT: awt r7, 3
; CHECK-NEXT: uj r4
define i16 @call_twice(i16 %a, i16 %b) {
  %x = call i16 @ext(i16 %a)
  %y = call i16 @ext(i16 %b)
  %z = add i16 %x, %y
  ret i16 %z
}

; The assembled call is two words; the linker fills the second one in.
; OBJ:      rj r4, 0
; OBJ-NEXT: R_MERA400_WORD16 void_ext
; OBJ:      rj r4, 0
; OBJ-NEXT: R_MERA400_WORD16 ext

