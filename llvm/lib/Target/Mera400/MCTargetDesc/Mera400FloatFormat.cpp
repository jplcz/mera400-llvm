//===-- Mera400FloatFormat.cpp - Native MERA-400 floating point -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Mera400FloatFormat.h"
#include <cmath>

using namespace llvm;
using namespace llvm::Mera400;

namespace {
const uint64_t SignBit = 1ULL << 63;
// The mantissa has 40 bits, so the low 24 of the 64 bits used here are
// dropped.
const uint64_t DroppedBits = 0xffffffULL;
const uint64_t HalfOfDropped = 0x800000ULL;

// A normalised two's complement mantissa has its two top bits different
// (0.1xxx or 1.0xxx), so that the first significant bit is always in the same
// place.
bool isNormalised(uint64_t M) { return ((M ^ (M << 1)) & SignBit) != 0; }
} // namespace

FloatConvStatus Mera400::convertDoubleToNativeFloat(double F,
                                                    bool RoundToNearest,
                                                    uint16_t Words[3]) {
  if (std::isnan(F))
    return FloatConvStatus::NotANumber;
  if (std::isinf(F))
    return FloatConvStatus::Overflow;

  int Exp;
  double Mant = std::frexp(F, &Exp);
  // A double has 53 significant bits, so this scaling is exact.
  uint64_t M = static_cast<uint64_t>(static_cast<int64_t>(std::ldexp(Mant, 63)));

  auto Normalise = [&] {
    while (M != 0 && !isNormalised(M)) {
      M <<= 1;
      --Exp;
    }
  };
  Normalise();

  if (RoundToNearest && M != 0) {
    // Round to nearest, ties to even: round up when the dropped part is more
    // than half, or exactly half and the lowest kept bit is 1 (odd). This
    // works on the two's complement bits for negative numbers too. A positive
    // mantissa can overflow the signed range; that means it was just below
    // 1.0 and rounds up to exactly 1.0, that is 0.1000... with the exponent
    // one higher.
    uint64_t Dropped = M & DroppedBits;
    bool Odd = (M & (DroppedBits + 1)) != 0;
    bool WasPositive = !(M & SignBit);
    M &= ~DroppedBits;
    if (Dropped > HalfOfDropped || (Dropped == HalfOfDropped && Odd))
      M += DroppedBits + 1;
    if (WasPositive && (M & SignBit)) {
      M = SignBit >> 1;
      ++Exp;
    }
    // A negative mantissa can round up to -0.5 exactly (1.1000...), which is
    // not normalised.
    Normalise();
  }
  M &= ~DroppedBits;

  if (M == 0)
    Exp = 0;
  Words[0] = M >> 48;
  Words[1] = M >> 32;
  Words[2] = ((M >> 16) & 0xff00) | (Exp & 0xff);
  return Exp > 127    ? FloatConvStatus::Overflow
         : Exp < -128 ? FloatConvStatus::Underflow
                      : FloatConvStatus::Ok;
}
