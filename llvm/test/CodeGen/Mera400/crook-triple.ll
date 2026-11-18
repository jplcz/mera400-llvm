; RUN: llc -mtriple=mera400-unknown-none < %s > %t.none.s
; RUN: llc -mtriple=mera400-unknown-crook < %s > %t.crook.s
; RUN: diff %t.none.s %t.crook.s
; RUN: FileCheck %s < %t.crook.s
; RUN: llc -mtriple=mera400-unknown-crook -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck --check-prefix=OBJ %s

; The CROOK triple is accepted and generates exactly the bare target's code.
; CHECK-LABEL: add:
; CHECK:       aw r1, r2
; CHECK-NEXT:  uj r4
; OBJ-LABEL: <add>:
; OBJ:       aw r1, r2
define i16 @add(i16 %a, i16 %b) {
  %r = add i16 %a, %b
  ret i16 %r
}
