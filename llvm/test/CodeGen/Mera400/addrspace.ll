; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck --check-prefix=OBJ %s

; A pointer in address space 0 is the address of a 16-bit word, so adding one
; element of an i16 array adds 1, not 2. A pointer in address space 1 is the
; address of an 8-bit byte. Casting a word pointer to a byte pointer doubles
; the address (shift left one bit, "slz"); the other direction drops the
; octet selector (shift right one bit, "srz").

@arr = external global [10 x i16]
@tab = global [2 x ptr] [ptr @arr, ptr getelementptr ([10 x i16], ptr @arr, i16 0, i16 3)]

%pair = type { i16, i16 }

; CHECK-LABEL: to_byte:
; CHECK:      slz r1
; CHECK-NEXT: uj r4
define ptr addrspace(1) @to_byte(ptr %p) {
  %q = addrspacecast ptr %p to ptr addrspace(1)
  ret ptr addrspace(1) %q
}

; CHECK-LABEL: to_word:
; CHECK:      srz r1
; CHECK-NEXT: uj r4
define ptr @to_word(ptr addrspace(1) %p) {
  %q = addrspacecast ptr addrspace(1) %p to ptr
  ret ptr %q
}

; CHECK-LABEL: byte_of_word:
; CHECK:      slz r1
; CHECK-NEXT: lwt r2, 0
; CHECK-NEXT: lb r2, r1
; CHECK-NEXT: lw r1, r2
; CHECK-NEXT: uj r4
define i16 @byte_of_word(ptr %p) {
  %q = addrspacecast ptr %p to ptr addrspace(1)
  %v = load i8, ptr addrspace(1) %q
  %z = zext i8 %v to i16
  ret i16 %z
}

; CHECK-LABEL: global_as_bytes:
; CHECK:      lw r1, arr
; CHECK-NEXT: slz r1
; CHECK-NEXT: uj r4
define ptr addrspace(1) @global_as_bytes() {
  ret ptr addrspace(1) addrspacecast (ptr @arr to ptr addrspace(1))
}

; CHECK-LABEL: struct_field:
; CHECK:      tw r1, 1+r1
; CHECK-NEXT: uj r4
define i16 @struct_field(ptr %p) {
  %f = getelementptr %pair, ptr %p, i16 0, i32 1
  %v = load i16, ptr %f
  ret i16 %v
}

; CHECK-LABEL: array_const:
; CHECK:      awt r1, 9
; CHECK-NEXT: uj r4
define ptr @array_const(ptr %p) {
  %q = getelementptr [4 x i16], ptr %p, i16 2, i16 1
  ret ptr %q
}

; CHECK-LABEL: word_index:
; CHECK:      aw r1, r2
; CHECK-NEXT: uj r4
define ptr @word_index(ptr %p, i16 %i) {
  %q = getelementptr i16, ptr %p, i16 %i
  ret ptr %q
}

; The same rule applies to constant addresses in data (@tab): a word pointer
; holds a word address, written as words(byte expression).
; CHECK-LABEL: tab:
; CHECK:       .short words(arr)
; CHECK-NEXT:  .short words(arr+6)

; OBJ:      slz r1
; OBJ:      srz r1
