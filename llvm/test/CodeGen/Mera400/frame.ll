; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck --check-prefix=OBJ %s

; Stack frames. R7 is the stack pointer and counts words; the frame is
; allocated in the first instruction and released just before the return, so
; callers never adjust R7 around a call. There is no frame pointer. The
; frame holds, from the bottom, outgoing call arguments, local objects,
; spill slots and saved registers. A function that makes a call saves its
; return address (R4); R5 and R6 are saved when used.

declare void @use(ptr)
declare i16 @ext(i16)
declare i16 @args5(i16, i16, i16, i16, i16)

; CHECK-LABEL: leaf_nospill:
; CHECK:      aw r1, r2
; CHECK-NEXT: uj r4
define i16 @leaf_nospill(i16 %a, i16 %b) {
  %r = add i16 %a, %b
  ret i16 %r
}

; CHECK-LABEL: leaf_object:
; CHECK:      awt r7, -4
; CHECK-NEXT: lwt r1, 1
; CHECK-NEXT: rw r1, 2+r7
; CHECK-NEXT: tw r1, 3+r7
; CHECK-NEXT: rw r1, r7
; CHECK-NEXT: awt r7, 4
; CHECK-NEXT: uj r4
define void @leaf_object() {
  %a = alloca [4 x i16]
  %e = getelementptr [4 x i16], ptr %a, i16 0, i16 2
  store volatile i16 1, ptr %e
  %f = getelementptr [4 x i16], ptr %a, i16 0, i16 3
  %v = load volatile i16, ptr %f
  store volatile i16 %v, ptr %a
  ret void
}

; CHECK-LABEL: pressure:
; CHECK:      awt r7, -3
; CHECK-NEXT: rw r5, 2+r7
; CHECK-NEXT: rw r6, 1+r7
; CHECK-NEXT: lw r2, r1
; CHECK-NEXT: tw r1, r2
; CHECK-NEXT: tw r3, 1+r2
; CHECK-NEXT: rw r3, r7
; CHECK-NEXT: lw r5, r1
; CHECK-NEXT: aw r5, r3
; CHECK-NEXT: tw r6, 2+r2
; CHECK-NEXT: aw r6, r5
; CHECK-NEXT: tw r3, 3+r2
; CHECK-NEXT: aw r3, r6
; CHECK-NEXT: tw r5, 4+r2
; CHECK-NEXT: aw r5, r3
; CHECK-NEXT: tw r6, 5+r2
; CHECK-NEXT: aw r5, r6
; CHECK-NEXT: tw r2, 6+r2
; CHECK-NEXT: aw r5, r2
; CHECK-NEXT: xr r1, r2
; CHECK-NEXT: tw r2, r7
; CHECK-NEXT: xr r1, r2
; CHECK-NEXT: xr r1, r6
; CHECK-NEXT: aw r1, r5
; CHECK-NEXT: tw r6, 1+r7
; CHECK-NEXT: tw r5, 2+r7
; CHECK-NEXT: awt r7, 3
; CHECK-NEXT: uj r4
define i16 @pressure(ptr %p) {
  %p1 = getelementptr i16, ptr %p, i16 1
  %p2 = getelementptr i16, ptr %p, i16 2
  %p3 = getelementptr i16, ptr %p, i16 3
  %p4 = getelementptr i16, ptr %p, i16 4
  %p5 = getelementptr i16, ptr %p, i16 5
  %p6 = getelementptr i16, ptr %p, i16 6
  %v0 = load volatile i16, ptr %p
  %v1 = load volatile i16, ptr %p1
  %v2 = load volatile i16, ptr %p2
  %v3 = load volatile i16, ptr %p3
  %v4 = load volatile i16, ptr %p4
  %v5 = load volatile i16, ptr %p5
  %v6 = load volatile i16, ptr %p6
  %s1 = add i16 %v0, %v1
  %s2 = add i16 %s1, %v2
  %s3 = add i16 %s2, %v3
  %s4 = add i16 %s3, %v4
  %s5 = add i16 %s4, %v5
  %s6 = add i16 %s5, %v6
  %t1 = xor i16 %v0, %v6
  %t2 = xor i16 %t1, %v1
  %t3 = xor i16 %t2, %v5
  %r = add i16 %s6, %t3
  ret i16 %r
}

; CHECK-LABEL: nonleaf:
; CHECK:      awt r7, -1
; CHECK-NEXT: rw r4, r7
; CHECK-NEXT: rj r4, ext
; CHECK-NEXT: awt r1, 1
; CHECK-NEXT: tw r4, r7
; CHECK-NEXT: awt r7, 1
; CHECK-NEXT: uj r4
define i16 @nonleaf(i16 %a) {
  %r = call i16 @ext(i16 %a)
  %s = add i16 %r, 1
  ret i16 %s
}

; CHECK-LABEL: nested:
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
define i16 @nested(i16 %a, i16 %b) {
  %x = call i16 @ext(i16 %a)
  %y = call i16 @ext(i16 %b)
  %z = add i16 %x, %y
  ret i16 %z
}

; CHECK-LABEL: object_and_call:
; CHECK:      awt r7, -4
; CHECK-NEXT: rw r4, 3+r7
; CHECK-NEXT: rw r1, r7
; CHECK-NEXT: lw r1, r7
; CHECK-NEXT: rj r4, use
; CHECK-NEXT: tw r1, r7
; CHECK-NEXT: tw r4, 3+r7
; CHECK-NEXT: awt r7, 4
; CHECK-NEXT: uj r4
define i16 @object_and_call(i16 %a) {
  %p = alloca [3 x i16]
  store i16 %a, ptr %p
  call void @use(ptr %p)
  %v = load i16, ptr %p
  ret i16 %v
}

; CHECK-LABEL: big_frame:
; CHECK:      aw r7, -100
; CHECK-NEXT: lwt r1, 7
; CHECK-NEXT: rw r1, 99+r7
; CHECK-NEXT: aw r7, 100
; CHECK-NEXT: uj r4
define void @big_frame() {
  %a = alloca [100 x i16]
  %e = getelementptr [100 x i16], ptr %a, i16 0, i16 99
  store volatile i16 7, ptr %e
  ret void
}

; OBJ:      awt r7, -4
; OBJ:      rj r4, 0
; OBJ:      rw r4, 3+r7
; OBJ:      aw r7, -100
; OBJ-NEXT: lwt r1, 7
; OBJ-NEXT: rw r1, 99+r7
; OBJ-NEXT: aw r7, 100

