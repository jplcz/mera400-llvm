; RUN: llc -mtriple=mera400-unknown-none -verify-machineinstrs < %s | FileCheck %s --check-prefix=SMALL
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -verify-machineinstrs < %s | FileCheck %s --check-prefix=LARGE
; RUN: llc -mtriple=mera400-unknown-none < %s | %python %S/Inputs/byte-global-sim.py small | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -O0 < %s | %python %S/Inputs/byte-global-sim.py small | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large < %s | %python %S/Inputs/byte-global-sim.py large | FileCheck --check-prefix=SIM %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -O0 < %s | %python %S/Inputs/byte-global-sim.py large | FileCheck --check-prefix=SIM %s

; Byte sized accesses (i8 and, widened, i1) to a global whose address is a
; link time constant. A word pointer counts words but "rb"/"lb" take BYTE
; addresses, so "rb r1, g" stored to byte g (the word address used as a byte
; address) and a load that read the word at g disagreed (static _Bool in the
; large model). The DAG combiner did that: it replaces a word read-modify-write
; (the large model byte store) by a byte store at the same pointer, and
; canNarrowAccessByByteOffset now forbids it for word pointers. The simulator
; places @b at word 0x100, @f at 0x200 and @w at 0x300 and checks the effect
; on every octet, including that the neighbours are untouched.

@b = global [4 x i8] zeroinitializer, align 2
@f = global i1 false, align 2
@w = global i16 0, align 2

; Neither model may use "rb" or "lb" with the symbol itself as the (word)
; address.
; SMALL-NOT: {{[rl]b}} {{r[0-7]}}, {{[bfw](\+[0-9]+)?$}}
; LARGE-NOT: {{[rl]b}} {{r[0-7]}}, {{[bfw](\+[0-9]+)?$}}

define void @set_b0() {
  store i8 5, ptr @b, align 1
  ret void
}

define void @set_b1() {
  %p = getelementptr i8, ptr @b, i16 1
  store i8 7, ptr %p, align 1
  ret void
}

define void @set_b3() {
  %p = getelementptr i8, ptr @b, i16 3
  store i8 9, ptr %p, align 1
  ret void
}

define i8 @get_b2() {
  %p = getelementptr i8, ptr @b, i16 2
  %v = load i8, ptr %p, align 1
  ret i8 %v
}

define void @set_f() {
  store i1 true, ptr @f, align 1
  ret void
}

define void @clear_f() {
  store i1 false, ptr @f, align 1
  ret void
}

define i1 @get_f() {
  %v = load i1, ptr @f, align 1
  ret i1 %v
}

; A word read-modify-write that keeps the low byte and replaces the high one
; (what a bit field store looks like), then the same for the other byte.
define void @set_w_high(i16 %x) {
  %o = load i16, ptr @w, align 2
  %k = and i16 %o, 255
  %s = shl i16 %x, 8
  %n = or i16 %k, %s
  store i16 %n, ptr @w, align 2
  ret void
}

define void @set_w_low(i16 %x) {
  %o = load i16, ptr @w, align 2
  %k = and i16 %o, -256
  %m = and i16 %x, 255
  %n = or i16 %k, %m
  store i16 %n, ptr @w, align 2
  ret void
}

; SIM: byte globals ok
