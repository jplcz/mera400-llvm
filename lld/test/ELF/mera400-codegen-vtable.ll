; REQUIRES: mera400
; RUN: llc -mtriple=mera400-unknown-none %s -o %t.s
; RUN: FileCheck %s --check-prefix=ASM < %t.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t.s -o %t.o
; RUN: ld.lld -m mera400elf -Ttext=0x100 %t.o -o %t.out
; RUN: llvm-objdump -s -j .rodata -j .data %t.out | FileCheck %s

;; A word pointer holds a word address, but ELF symbols are byte addresses.
;; Pointers in data (here a C++-style vtable and an object that points to it)
;; are therefore emitted as words(sym), which is a WORD16 relocation.
;; "a" is at byte 0x100 (word 0x80), "b" at 0x104 (word 0x82), the vtable at
;; byte 0x11a (word 0x8d).
; ASM:      vt:
; ASM-NEXT:   .short words(a)
; ASM-NEXT:   .short words(b)
; ASM:      obj:
; ASM-NEXT:   .short words(vt)
; ASM-NEXT:   .short 7

; CHECK: Contents of section .rodata:
; CHECK-NEXT: 011a 00800082
; CHECK: Contents of section .data:
; CHECK-NEXT: 011e 008d0007

%S = type { ptr, i16 }

define i16 @a(ptr %this) {
  ret i16 1
}

define i16 @b(ptr %this) {
  ret i16 2
}

@vt = constant { [2 x ptr] } { [2 x ptr] [ptr @a, ptr @b] }
@obj = global %S { ptr getelementptr inbounds ({ [2 x ptr] }, ptr @vt, i32 0, i32 0, i32 0), i16 7 }

define i16 @call_virtual(ptr %o) {
  %vp = load ptr, ptr %o
  %slot = getelementptr inbounds ptr, ptr %vp, i16 1
  %fn = load ptr, ptr %slot
  %r = call i16 %fn(ptr %o)
  ret i16 %r
}
