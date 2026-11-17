; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj | llvm-objdump -d --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ

; A switch is a chain of compares: there are no jump tables.

declare void @a()
declare void @b()
declare void @c()

; CHECK-LABEL: sw:
; CHECK-NOT:   .rodata
; CHECK:       cwt r1,
; CHECK:       je
define void @sw(i16 %x) {
entry:
  switch i16 %x, label %def [
    i16 1, label %l1
    i16 2, label %l2
    i16 3, label %l3
    i16 4, label %l1
    i16 5, label %l2
    i16 6, label %l3
    i16 7, label %l1
    i16 8, label %l2
  ]
l1:
  call void @a()
  ret void
l2:
  call void @b()
  ret void
l3:
  call void @c()
  ret void
def:
  ret void
}

; OBJ-LABEL: <sw>:
; OBJ:       cwt r1,
