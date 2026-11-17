# REQUIRES: mera400
# RUN: rm -rf %t && split-file %s %t
# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %t/a.s -o %t/a.o

## lld processes --defsym and the assignments of a linker script in command
## line order (as GNU ld does), and evaluates "__heap_end = __stack_top - ..."
## when it reaches it. This is expected lld behaviour, not a bug; a script that
## wants to be parameterized from the command line must define its default with
## PROVIDE() and the --defsym must come before the script.

## --defsym first: PROVIDE is ignored, __heap_end uses the command line value.
# RUN: ld.lld --defsym=__stack_top=0x8000 -T %t/provide.ld %t/a.o -o %t/first
# RUN: llvm-nm %t/first | FileCheck %s --check-prefix=FIRST
# FIRST: 00007f00 A __heap_end
# FIRST: 00008000 A __stack_top

## --defsym after the script: __heap_end was already computed from the default.
# RUN: ld.lld -T %t/provide.ld --defsym=__stack_top=0x8000 %t/a.o -o %t/after
# RUN: llvm-nm %t/after | FileCheck %s --check-prefix=AFTER
# AFTER: 0000fefe A __heap_end
# AFTER: 00008000 A __stack_top

## A plain assignment (no PROVIDE) in the script overrides an earlier --defsym.
# RUN: ld.lld --defsym=__stack_top=0x8000 -T %t/plain.ld %t/a.o -o %t/plain
# RUN: llvm-nm %t/plain | FileCheck %s --check-prefix=PLAIN
# PLAIN: 0000fefe A __heap_end
# PLAIN: 0000fffe A __stack_top

#--- a.s
	.text
	.globl _start
_start:
	.word 1

#--- provide.ld
PROVIDE(__stack_top = 0xFFFE);
__stack_size = 0x100;
__heap_end = __stack_top - __stack_size;
SECTIONS { .text : { *(.text*) } }

#--- plain.ld
__stack_top = 0xFFFE;
__stack_size = 0x100;
__heap_end = __stack_top - __stack_size;
SECTIONS { .text : { *(.text*) } }
