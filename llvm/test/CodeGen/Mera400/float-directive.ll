; RUN: llc -mtriple=mera400-unknown-none < %s | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.a.o
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %S/Inputs/float-directive.s -o %t.b.o
; RUN: llvm-objdump -s -j .data %t.a.o | tail -n +3 > %t.a.txt
; RUN: llvm-objdump -s -j .data %t.b.o | tail -n +3 > %t.b.txt
; RUN: diff %t.a.txt %t.b.txt

; For values that the native format represents exactly, the data the compiler
; emits for a double is the same as what the assembler's .float directive
; makes (three words), followed by one zero padding word. For other values
; they can differ in the last bit: .float drops the extra bits like EMAS does
; (see MC/Mera400/float-truncation.s) while the compiler rounds to nearest
; (see float-const.ll), and writes the exact words with .quad.

@a = global double 1.500000e+00
@b = global double -2.000000e+00
@d = global double 1.024000e+03
@e = global double -3.000000e+00
