; RUN: llc -O0 -mtriple=mera400-unknown-none -target-abi=large < %s | FileCheck %s

; At -O0 the fast register allocator ran out of registers on the paired 32-bit
; add of the large model address arithmetic when the operands stayed live.

; CHECK-LABEL: f:
; CHECK: ac
define i16 @f(ptr addrspace(1) %p) "frame-pointer"="all" {
  %q = getelementptr inbounds i8, ptr addrspace(1) %p, i16 2
  %a = load i16, ptr addrspace(1) %p, align 2
  %b = load i16, ptr addrspace(1) %q, align 2
  %r = add i16 %a, %b
  ret i16 %r
}
