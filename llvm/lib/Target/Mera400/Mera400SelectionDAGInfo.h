//===-- Mera400SelectionDAGInfo.h - MERA-400 SelectionDAG info ----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the target's SelectionDAG node information (names and operand
// shapes of the custom nodes, such as the Mera400ISD operations).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MERA400SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_MERA400_MERA400SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "Mera400GenSDNodeInfo.inc"

namespace llvm {

class Mera400SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  Mera400SelectionDAGInfo();
  ~Mera400SelectionDAGInfo() override;
};

} // namespace llvm

#endif
