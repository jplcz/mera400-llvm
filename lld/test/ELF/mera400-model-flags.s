# REQUIRES: mera400
# RUN: rm -rf %t && split-file %s %t
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/large.s -o %t/large.o
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/small.s -o %t/small.o
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/neutral.s -o %t/neutral.o

## A large and a small object cannot be linked; both are named.
# RUN: not ld.lld %t/large.o %t/small.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=MIX
# RUN: not ld.lld %t/small.o %t/large.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=MIX
# MIX: error: cannot link the large memory model object {{.*}}large.o with the small memory model object {{.*}}small.o

## An object without a model (plain assembler) links with either, and the
## output gets the model of the others.
# RUN: ld.lld %t/large.o %t/neutral.o -o %t/l
# RUN: llvm-readobj -h %t/l | FileCheck %s --check-prefix=LARGE
# RUN: ld.lld %t/neutral.o %t/small.o -o %t/s
# RUN: llvm-readobj -h %t/s | FileCheck %s --check-prefix=SMALL
# RUN: ld.lld %t/neutral.o -o %t/n
# RUN: llvm-readobj -h %t/n | FileCheck %s --check-prefix=NEUTRAL
# LARGE: EF_MERA400_LARGE (0x1)
# SMALL: EF_MERA400_SMALL (0x2)
# NEUTRAL: Flags [ (0x0)

#--- large.s
	.model large
	.text
	.globl _start
_start:
	nop
#--- small.s
	.model small
	.text
	.globl other
other:
	nop
#--- neutral.s
	.text
	.globl third
third:
	nop
