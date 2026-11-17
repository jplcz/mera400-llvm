# REQUIRES: mera400
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t.o
# RUN: ld.lld -Ttext=0x100 --defsym=ext=0x110 %t.o -o %t
# RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

## The label offset (here 4 bytes into .data) and the `+3` words survive
## linking: the branch is relative to the next instruction.
# CHECK: 100: ujs 4 <d>
# CHECK: 102: ujs 9
# CHECK: 104: ujs 2 <d>
	.text
	.globl _start
_start:
	ujs d
	ujs ext+3
	ujs ext-3
	.data
	.word 1, 2
d:	nop
