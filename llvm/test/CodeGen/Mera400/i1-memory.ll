; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large < %s | FileCheck %s --check-prefix=LARGE

; An i1 in memory is a byte holding 0 or 1 (the optimizer creates such
; globals: GlobalOpt shrinks a global that only ever holds two constants to an
; i1, which used to be rejected as "not a 16, 32 or 64-bit access"). Loads
; and stores are byte accesses with a zero extension / truncation, also
; through a word pointer (address space 0).

@flag = internal global i1 false
@init = internal global i1 true

; CHECK-LABEL: set:
; CHECK:       lw r2, flag
; CHECK-NEXT:  slz r2
; CHECK:       rb r1, r2
; LARGE-LABEL: set:
; LARGE-NOT:   rb
; LARGE:       tw r2, flag
; LARGE:       rw r1, flag
define void @set(i1 %v) {
  store i1 %v, ptr @flag
  ret void
}

; CHECK-LABEL: get:
; CHECK:       lb r{{[0-9]}}, r{{[0-9]}}
; CHECK:       nr r{{[0-9]}}, 1
; O0-LABEL: get:
; O0:          lb r
; O0:          nr r
define i16 @get() {
  %v = load i1, ptr @flag
  %r = zext i1 %v to i16
  ret i16 %r
}

; Through a byte pointer (address space 1).
; CHECK-LABEL: bytep:
; CHECK:       lb
; CHECK:       rb
define void @bytep(ptr addrspace(1) %p, ptr addrspace(1) %q) {
  %v = load i1, ptr addrspace(1) %p
  store i1 %v, ptr addrspace(1) %q
  ret void
}

; A stack slot.
; CHECK-LABEL: local:
define i16 @local(i1 %v, ptr %out) {
  %a = alloca i1
  store i1 %v, ptr %a
  call void @take(ptr %a)
  %l = load i1, ptr %a
  %r = select i1 %l, i16 7, i16 9
  ret i16 %r
}
declare void @take(ptr)

; CHECK:       .type init,@object
; CHECK:       .byte 1
