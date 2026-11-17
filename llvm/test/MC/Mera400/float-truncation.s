; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o %t
; RUN: llvm-readobj --hex-dump=.text %t | FileCheck %s

; .float keeps the behaviour of EMAS: the digits of the mantissa that do not
; fit in 40 bits are dropped (the EMAWP library that EMAS uses does this), so
; a negative number is rounded towards minus infinity. These words are what
; awp_from_double() of the EMAWP library gives for the same literals.
; The compiler rounds constants to the nearest value instead; it writes them
; as exact words, not with .float, so the two never disagree about the
; meaning of a given piece of data (see CodeGen/Mera400/float-directive.ll).
; 0.1 is 0.8 * 2^-3 and 0.8 * 2^39 is 0x6666666666.666..., so the nearest
; mantissa would be 0x6666666667, not the 0x6666666666 given here.
; CHECK: 0x00000000 66666666 66fd9999 999999fd 55555555
; CHECK-NEXT: 0x00000010 55ffaaaa aaaaaaff 40000000 0001bfff
; CHECK-NEXT: 0x00000020 ffffff01 9f8ca467 380e

.float 0.1, -0.1, 0.3333333333333333, -0.3333333333333333
.float 1.000000000001819, -1.000000000001819, -12345.6789
