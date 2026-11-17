; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --hex-dump=.text %t | FileCheck %s
; RUN: not llvm-mc -triple=mera400-unknown-none -filetype=obj %S/Inputs/directive-errors.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

; Expected words come from the EMAS acceptance tests (pragmas, dword, float).
; CHECK: 0x00000000 00010002 00030004 00000000 00040004
; CHECK-NEXT: 0x00000010 00010001 00010001 00011170 fffeee90
; CHECK-NEXT: 0x00000020 61626300 64640000 80000000 00014000
; CHECK-NEXT: 0x00000030 00000001 84000061 41420700 00f003e8

.word 1, 2, 3, 4
; .res reserves words, optionally filled
.res 2
.res 2, 4
.res 4, 1
; .dword is big-endian: high word first
.dword 70000, -70000
; .ascii packs two characters per word, high byte first
.ascii "abc"
.asciiz "dd"
; .float is three words: 40-bit mantissa, 8-bit exponent
.float -2.0, 1.0
; flag and character literals, digit separators
.word ?ZE, 'a', '\101\102', '\7\0', 0b1111_0000, 1_000

; ERR: error: Value won't fit in a DWORD: 4294967296
; ERR: error: Floating point overflow
; ERR: error: Floating point underflow
; ERR: error: Invalid escape sequence (value too big): "\400"
; ERR: error: Duplicated flag: 'Z'
; ERR: error: .res count requires an integer expression, got float
