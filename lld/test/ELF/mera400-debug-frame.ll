; REQUIRES: mera400
; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 -force-dwarf-frame-section %s -o %t.s
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t.s -o %t.o
; RUN: ld.lld -m mera400elf -Ttext=0x100 %t.o -o %t
; RUN: llvm-dwarfdump --debug-frame %t | FileCheck %s

;; The call frame information of a linked ELF file (kept so that a debugger can
;; unwind the stack; the flat image for the device does not carry it). The
;; function addresses in the frame descriptions are byte addresses, resolved
;; by 32-bit relocations in a non-allocated section.

; CHECK: FDE cie=00000000 pc=00000100...00000104
; CHECK: FDE cie=00000000 pc=00000104...00000114
; CHECK:   0x104: CFA=R7: IC=DW_OP_breg4 R4+0, DW_OP_lit1, DW_OP_shl
; CHECK:   0x108: CFA=R7+2: IC=DW_OP_call_frame_cfa, DW_OP_consts -2, DW_OP_plus, DW_OP_deref_size 0x2, DW_OP_lit1, DW_OP_shl

declare i16 @leaf(i16)

define i16 @f(i16 %a) {
  %r = add i16 %a, 1
  ret i16 %r
}

define i16 @g(i16 %a) {
  %r = call i16 @f(i16 %a)
  %s = add i16 %r, 1
  ret i16 %s
}
