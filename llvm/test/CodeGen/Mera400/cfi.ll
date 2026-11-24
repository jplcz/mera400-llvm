; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 -force-dwarf-frame-section < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 -force-dwarf-frame-section < %s \
; RUN:   | llvm-mc -triple=mera400-unknown-none -filetype=obj -o %t.o
; RUN: llvm-dwarfdump --debug-frame %t.o | FileCheck --check-prefix=DUMP %s
; RUN: llc -mtriple=mera400-unknown-none -enable-new-pm=0 < %s | FileCheck --check-prefix=NOCFI %s

; Call frame information lets a debugger unwind the stack. R7 is the stack
; pointer and is described in bytes; the return address is in R4 as a word
; address, so the rule for the caller's IC (DWARF register 8) multiplies it by
; two. It is emitted only when requested (-g or -force-dwarf-frame-section).

declare i16 @ext(i16)

; A leaf function keeps its return address in R4 and has no frame.
; CHECK-LABEL: leaf:
; CHECK:       .cfi_startproc
; CHECK-NOT:   .cfi_def_cfa_offset
; CHECK:       .cfi_endproc
define i16 @leaf(i16 %a) {
  %r = add i16 %a, 1
  ret i16 %r
}

; A function that calls saves R4 in its frame (one word, 2 bytes).
; CHECK-LABEL: caller:
; CHECK:       rw r4, r7
; CHECK-NEXT:  .cfi_def_cfa_offset 2
; CHECK-NEXT:  .cfi_escape 0x16, 0x08, 0x08, 0x9c, 0x11, 0x7e, 0x22, 0x94, 0x02, 0x31, 0x24
; CHECK:       tw r4, r7
; CHECK-NEXT:  awt r7, 1
; CHECK-NEXT:  .cfi_def_cfa_offset 0
; CHECK-NEXT:  .cfi_escape 0x16, 0x08, 0x04, 0x74, 0x00, 0x31, 0x24
; CHECK:       .cfi_endproc
define i16 @caller(i16 %a) {
  %r = call i16 @ext(i16 %a)
  %s = add i16 %r, 1
  ret i16 %s
}

; A leaf with a frame only moves the CFA; its return address stays in R4.
; CHECK-LABEL: local:
; CHECK:       .cfi_def_cfa_offset 2
; CHECK-NOT:   .cfi_escape
; CHECK:       .cfi_def_cfa_offset 0
; CHECK:       .cfi_endproc
define void @local(ptr %p) {
  %x = alloca i16
  store volatile i16 1, ptr %x
  ret void
}

; DUMP: CIE
; DUMP:   Return address column: 8
; DUMP:   DW_CFA_def_cfa: R7 +0
; DUMP:   DW_CFA_val_expression: IC DW_OP_breg4 R4+0, DW_OP_lit1, DW_OP_shl
; DUMP: FDE
; DUMP: FDE
; DUMP:   DW_CFA_def_cfa_offset: +2
; DUMP:   DW_CFA_val_expression: IC DW_OP_call_frame_cfa, DW_OP_consts -2, DW_OP_plus, DW_OP_deref_size 0x2, DW_OP_lit1, DW_OP_shl

; NOCFI-NOT: .cfi_
