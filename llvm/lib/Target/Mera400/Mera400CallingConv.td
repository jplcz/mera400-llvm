//===-- Mera400CallingConv.td - MERA-400 calling convention ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// i8 and i1 values are promoted to i16 by type legalization, and i32 and i64
// are split into 16-bit parts, high part first. Every part travels in R1, R2
// and R3 in turn, and is returned the same way. Parts that do not fit go on
// the stack: one word each, the first one at the lowest address, which is R7
// of the caller at the time of the call. The analysis loop in
// Mera400ISelLowering.cpp adds one rule: a 32-bit value that would be split
// between R3 and the stack goes to the stack as a whole. Results that need
// more than R1..R3 (i64) are returned through a hidden pointer argument.
//
//===----------------------------------------------------------------------===//

def RetCC_Mera400 : CallingConv<[
  CCIfType<[i16], CCAssignToReg<[R1, R2, R3]>>
]>;

def CC_Mera400 : CallingConv<[
  CCIfType<[i16], CCAssignToReg<[R1, R2, R3]>>,
  CCIfType<[i16], CCAssignToStack<2, 2>>
]>;

// R7 is the stack pointer and is handled by the frame lowering. Callers can
// rely on R5 and R6 surviving a call.
def CSR_Mera400 : CalleeSavedRegs<(add R5, R6)>;

// The registers a function saves in its frame: R4 holds the return address,
// so it is saved (by Mera400FrameLowering) only when the function calls.
def CSR_Mera400_Frame : CalleeSavedRegs<(add R4, R5, R6)>;
