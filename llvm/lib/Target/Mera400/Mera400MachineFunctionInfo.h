//===-- Mera400MachineFunctionInfo.h - Per-function state -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_MERA400_MERA400MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

/// Information that argument lowering (which sees the function's parameters)
/// hands to the lowering of va_start (which does not).
class Mera400MachineFunctionInfo : public MachineFunctionInfo {
  // The stack slot where the variadic arguments start: the first stack word
  // after the named arguments that did not fit in R1..R3. -1 if the function
  // is not variadic.
  int VarArgsFrameIndex = -1;

public:
  Mera400MachineFunctionInfo(const Function &, const TargetSubtargetInfo *) {}

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }
};

} // end namespace llvm

#endif
