//===-- Mera400TargetStreamer.h - MERA-400 target streamers ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the target streamer, which carries target-specific directives
// (here ".model") alongside the ordinary instruction stream.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MCTARGETDESC_MERA400TARGETSTREAMER_H
#define LLVM_LIB_TARGET_MERA400_MCTARGETDESC_MERA400TARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {

class formatted_raw_ostream;
class MCInstPrinter;
class MCSubtargetInfo;

// Records the memory model (".model small" or ".model large"): the assembly
// streamer prints the directive and the object streamer sets the matching
// bit of the ELF header flags.
class Mera400TargetStreamer : public MCTargetStreamer {
public:
  explicit Mera400TargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

  virtual void emitModel(bool Large) = 0;

  // The compiler states the large model in its assembly output but leaves out
  // the default, so that existing small model output does not change; an
  // object file always records its model.
  void emitCompilerModel(bool Large) {
    if (Large)
      emitModel(true);
    else
      emitImplicitSmallModel();
  }

private:
  virtual void emitImplicitSmallModel() = 0;
};

MCTargetStreamer *createMera400AsmTargetStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint);
MCTargetStreamer *createMera400ObjectTargetStreamer(MCStreamer &S,
                                                    const MCSubtargetInfo &STI);
MCTargetStreamer *createMera400NullTargetStreamer(MCStreamer &S);

} // namespace llvm

#endif
