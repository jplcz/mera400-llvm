# REQUIRES: mera400
# RUN: rm -rf %t && split-file %s %t
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/r.s -o %t/r.o
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/far.s -o %t/far.o

## R_MERA400_32 holds a 32-bit big-endian byte address and R_MERA400_HI16 and
## R_MERA400_LO16 its two words. "tab" is at 0x40, so tab+1 is 0x41.
# RUN: ld.lld -Ttext=0x10 %t/r.o -o %t/ok
# RUN: llvm-objdump -s -j .text %t/ok | FileCheck %s
# CHECK: 0010 40400000 40800041 00000041 00000000

## The target of the largest byte address, 0x1ffff, is accepted.
# RUN: ld.lld -Ttext=0x10 --defsym=far=0x1ffff %t/far.o -o %t/edge
# RUN: llvm-objdump -s -j .text %t/edge | FileCheck %s --check-prefix=EDGE
# EDGE: 0010 40400001 4080ffff 0001ffff

## A byte address of 128 KiB or more does not fit the memory.
# RUN: not ld.lld -Ttext=0x10 --defsym=far=0x20000 %t/far.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANGE
# RANGE-DAG: error: {{.*}}relocation R_MERA400_HI16 out of range: 131072 is not in [0, 131071]
# RANGE-DAG: error: {{.*}}relocation R_MERA400_LO16 out of range
# RANGE-DAG: error: {{.*}}relocation R_MERA400_32 out of range

#--- r.s
	.model large
	.text
	.globl _start
_start:
	lw r1, hi(tab+1)
	lw r2, lo(tab+1)
	.long tab+1
	.long 0
	.data
	.org 0x10
tab:
	.long 0
#--- far.s
	.model large
	.text
	.globl _start
_start:
	lw r1, hi(far)
	lw r2, lo(far)
	.long far
