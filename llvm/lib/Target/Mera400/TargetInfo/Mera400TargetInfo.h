//===-- Mera400TargetInfo.h - MERA-400 target ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the accessor for the global Target object that identifies the
// MERA-400 to the target registry.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_TARGETINFO_MERA400TARGETINFO_H
#define LLVM_LIB_TARGET_MERA400_TARGETINFO_MERA400TARGETINFO_H

namespace llvm {
class Target;
Target &getTheMera400Target();
} // namespace llvm

#endif
