# REQUIRES: mera400
# RUN: rm -rf %t && split-file %s %t
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/a.s -o %t/a.o
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/b.s -o %t/b.o
# RUN: ld.lld -m mera400elf -Ttext=0x100 %t/a.o %t/b.o -o %t/out
# RUN: llvm-objdump -d --no-show-raw-insn %t/out | FileCheck %s
# RUN: llvm-objdump -s -j .data %t/out | FileCheck %s --check-prefix=DATA

## Section addresses are bytes, MERA-400 addresses are words: _start is at byte
## 0x100 (word 0x80), helper at byte 0x10a, table at byte 0x110 (word 0x88).
## Sections are packed word to word: there is no page padding between code and
## data. WORD16 stores the word address, PCREL7 a word displacement from the
## next instruction.
# CHECK-LABEL: <_start>:
# CHECK-NEXT:  100: lwt r1, 3
# CHECK-NEXT:  102: lw r2, 136
# CHECK-NEXT:  106: ujs 1 <helper>
# CHECK-NEXT:  108: hlt 0
# CHECK-LABEL: <helper>:
# CHECK-NEXT:  10a: awt r1, -1
# CHECK-NEXT:  10c: uj 128 <_start>
# DATA: 0110 00010002 00800085

## The flat binary holds only the section contents, not ELF headers, and code
## is followed directly by data.
# RUN: ld.lld -m mera400elf --oformat=binary -Ttext=0x100 %t/a.o %t/b.o -o %t/out.bin
# RUN: od -An -v -tx1 %t/out.bin | FileCheck %s --check-prefix=BIN
# BIN: d4 43 40 80 00 88 e0 01 ec 00 c2 41 f0 00 00 80
# BIN-NEXT: 00 01 00 02 00 80 00 85

## A program past 65536 words, an out-of-range short branch and a short branch
## that is not word aligned are rejected.
# RUN: not ld.lld -m mera400elf -Ttext=0x1fffe %t/a.o %t/b.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=BIG
# BIG: error: program too large: section '.text'

# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/far.s -o %t/far.o
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/mid.s -o %t/mid.o
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/end.s -o %t/end.o
# RUN: not ld.lld -m mera400elf %t/far.o %t/mid.o %t/end.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=FAR
# FAR: error: {{.*}}relocation R_MERA400_PCREL7 out of range: {{[0-9]+}} is not in [-63, 63]

#--- a.s
	.text
	.globl _start
_start:
	lwt r1, 3
	lw r2, table
	ujs helper
	hlt
	.data
table:
	.word 1, 2, _start, helper
#--- b.s
	.text
	.globl helper
helper:
	awt r1, -1
	uj _start
#--- far.s
	.text
	.globl _start
_start:
	ujs far
#--- mid.s
	.text
	.space 128
#--- end.s
	.text
	.globl far
far:
	nop
