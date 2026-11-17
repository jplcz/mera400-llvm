# REQUIRES: mera400
# RUN: rm -rf %t && split-file %s %t
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/w.s -o %t/w.o
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/s.s -o %t/s.o
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/u.s -o %t/u.o

## The machine is inferred from the input, and -e selects the entry point.
# RUN: ld.lld -Ttext=0x100 -e other %t/w.o -o %t/ok
# RUN: llvm-readobj -h %t/ok | FileCheck %s --check-prefix=ENTRY
# ENTRY: Entry: 0x102

## A symbol at byte 0x20000 has word address 0x10000, which does not fit 16 bits.
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/h.s -o %t/h.o
# RUN: not ld.lld --defsym=far=0x20000 %t/h.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=HIGH
# HIGH: error: {{.*}}relocation R_MERA400_WORD16 out of range

## A word address needs an even byte address.
# RUN: not ld.lld --defsym=far=0x20001 %t/h.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ODD
# ODD: error: {{.*}}improper alignment for relocation R_MERA400_WORD16

## A byte value that exceeds 16 bits is rejected.
# RUN: not ld.lld -Ttext=0x10000 %t/s.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=SHORT
# SHORT: error: {{.*}}relocation R_MERA400_16 out of range

# RUN: not ld.lld %t/u.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNDEF
# UNDEF: error: undefined symbol: nowhere

## A linker script can place the program at address 0.
# RUN: echo 'SECTIONS { . = 0; .text : { *(.text) } }' > %t/ls
# RUN: ld.lld -T %t/ls %t/w.o -o %t/zero
# RUN: llvm-objdump -d --no-show-raw-insn %t/zero | FileCheck %s --check-prefix=ZERO
# ZERO: 0: nop

## Short values hold a word address in 7 bits (-63..63).
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/v.s -o %t/v.o
# RUN: ld.lld --defsym=far=0x7e %t/v.o -o %t/v.ok
# RUN: llvm-objdump -d --no-show-raw-insn %t/v.ok | FileCheck %s --check-prefix=V
# V: lwt r1, 63
# RUN: not ld.lld --defsym=far=0x80 %t/v.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=VHIGH
# VHIGH: error: {{.*}}relocation R_MERA400_WORD7 out of range: 64 is not in [-63, 63]
# RUN: not ld.lld --defsym=far=0x7f %t/v.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=VODD
# VODD: error: {{.*}}improper alignment for relocation R_MERA400_WORD7

#--- v.s
	.text
	.globl _start
_start:
	lwt r1, far
#--- w.s
	.text
	.globl _start, other
_start:
	nop
other:
	.word other
#--- h.s
	.text
	.globl _start
_start:
	.word far
#--- s.s
	.text
	.globl _start
_start:
	.short _start
#--- u.s
	.text
	.globl _start
_start:
	.word nowhere
