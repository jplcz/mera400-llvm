//===-- Mera400TargetStreamer.cpp - MERA-400 target streamers -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the ".model" directive: the assembly streamer prints it, the
// object streamer records it in the ELF header flags so the linker can reject
// mixing small and large model objects.
//
//===----------------------------------------------------------------------===//

#include "Mera400TargetStreamer.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

namespace {
class Mera400AsmTargetStreamer : public Mera400TargetStreamer {
  formatted_raw_ostream &OS;

public:
  Mera400AsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS)
      : Mera400TargetStreamer(S), OS(OS) {}

  void emitModel(bool Large) override {
    OS << (Large ? "\t.model large\n" : "\t.model small\n");
  }

private:
  void emitImplicitSmallModel() override {}
};

class Mera400ObjectTargetStreamer : public Mera400TargetStreamer {
public:
  Mera400ObjectTargetStreamer(MCStreamer &S) : Mera400TargetStreamer(S) {}

  void emitModel(bool Large) override {
    ELFObjectWriter &W = static_cast<MCELFStreamer &>(getStreamer()).getWriter();
    W.setELFHeaderEFlags(W.getELFHeaderEFlags() |
                         (Large ? ELF::EF_MERA400_LARGE
                                : ELF::EF_MERA400_SMALL));
  }

private:
  void emitImplicitSmallModel() override { emitModel(false); }
};

class Mera400NullTargetStreamer : public Mera400TargetStreamer {
public:
  Mera400NullTargetStreamer(MCStreamer &S) : Mera400TargetStreamer(S) {}
  void emitModel(bool) override {}

private:
  void emitImplicitSmallModel() override {}
};
} // namespace

MCTargetStreamer *
llvm::createMera400AsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                                     MCInstPrinter *) {
  return new Mera400AsmTargetStreamer(S, OS);
}

MCTargetStreamer *
llvm::createMera400ObjectTargetStreamer(MCStreamer &S,
                                        const MCSubtargetInfo &) {
  return new Mera400ObjectTargetStreamer(S);
}

MCTargetStreamer *llvm::createMera400NullTargetStreamer(MCStreamer &S) {
  return new Mera400NullTargetStreamer(S);
}
