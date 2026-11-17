; REQUIRES: mera400
; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=mera400-unknown-none %t/a.ll -o %t/a.s
; RUN: llc -mtriple=mera400-unknown-none %t/b.ll -o %t/b.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/a.s -o %t/a.o
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/b.s -o %t/b.o
; RUN: ld.lld -m mera400elf -Ttext=0x100 --entry=entry %t/a.o %t/b.o -o %t/out
; RUN: llvm-objdump -d --no-show-raw-insn %t/out | FileCheck %s
; RUN: llvm-objdump -t %t/out | FileCheck --check-prefix=SYMS %s

;; A call of the C library's memcpy (too long to expand inline) and of a
;; variadic function, linked with their definitions. The addresses of the
;; arrays are loaded as byte addresses: dst at byte 0x1c0 is 224 words, so
;; "lw r1, 224" is followed by "slz r1" (to 0x1c0), and src at byte 0x190 is
;; word 200. The size 40 is in r3. The call instruction holds the callee's
;; word address: memcpy at byte 0x130 is word 152 and sum at byte 0x166 is
;; word 179. The variadic arguments 30 and 12 are on the stack, in slots 0 and
;; 1 (the named argument 2 is in r1); the callee finds the first slot at the
;; byte address (R7 + 1 word of frame) * 2.
; CHECK-LABEL: <entry>:
; CHECK:       lw r1, 224
; CHECK-NEXT:  slz r1
; CHECK-NEXT:  lw r2, 200
; CHECK-NEXT:  slz r2
; CHECK-NEXT:  lwt r3, 40
; CHECK-NEXT:  rj r4, 152
; CHECK-NEXT:  lwt r1, 12
; CHECK-NEXT:  rw r1, 1+r7
; CHECK-NEXT:  lwt r1, 30
; CHECK-NEXT:  rw r1, r7
; CHECK-NEXT:  lwt r1, 2
; CHECK-NEXT:  rj r4, 179
; CHECK-LABEL: <memcpy>:
; CHECK:       lb r3, r2
; CHECK-NEXT:  rb r3, r6
; CHECK-LABEL: <sum>:
; CHECK:       lw r2, r7
; CHECK-NEXT:  awt r2, 1
; CHECK-NEXT:  slz r2

; SYMS-DAG: 000001c0 g O .bss {{.*}} dst
; SYMS-DAG: 00000190 g O .bss {{.*}} src
; SYMS-DAG: 00000130 g F .text {{.*}} memcpy
; SYMS-DAG: 00000166 g F .text {{.*}} sum

;--- a.ll
target triple = "mera400-unknown-none"
declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1)
declare i16 @sum(i16, ...)
@src = global [20 x i16] zeroinitializer
@dst = global [20 x i16] zeroinitializer
define i16 @entry() {
  call void @llvm.memcpy.p0.p0.i16(ptr @dst, ptr @src, i16 40, i1 false)
  %r = call i16 (i16, ...) @sum(i16 2, i16 30, i16 12)
  ret i16 %r
}

;--- b.ll
target triple = "mera400-unknown-none"
declare void @llvm.va_start.p0(ptr)
define ptr addrspace(1) @memcpy(ptr addrspace(1) %d, ptr addrspace(1) %s, i16 %n) {
  br label %loop
loop:
  %i = phi i16 [0, %0], [%i1, %loop]
  %sp = getelementptr i8, ptr addrspace(1) %s, i16 %i
  %dp = getelementptr i8, ptr addrspace(1) %d, i16 %i
  %b = load i8, ptr addrspace(1) %sp
  store i8 %b, ptr addrspace(1) %dp
  %i1 = add i16 %i, 1
  %c = icmp ult i16 %i1, %n
  br i1 %c, label %loop, label %done
done:
  ret ptr addrspace(1) %d
}
define i16 @sum(i16 %n, ...) {
  %ap = alloca ptr addrspace(1)
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, i16
  %b = va_arg ptr %ap, i16
  %s = add i16 %a, %b
  ret i16 %s
}
