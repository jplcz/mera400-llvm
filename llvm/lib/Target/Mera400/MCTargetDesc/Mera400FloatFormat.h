//===-- Mera400FloatFormat.h - Native MERA-400 floating point ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Conversion from IEEE double to the native floating point format of the
// MERA-400 AWP (the arithmetic processor). A native number occupies three
// 16-bit words: a 40-bit two's complement mantissa (the first two words and
// the high byte of the third) followed by an 8-bit exponent (the low byte of
// the third word). The assembler's .float directive and the compiler's
// floating point constants share this one implementation, but differ in
// rounding: .float truncates like EMAS (the EMAWP library), while the compiler
// rounds to nearest, ties to even.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MERA400_MCTARGETDESC_MERA400FLOATFORMAT_H
#define LLVM_LIB_TARGET_MERA400_MCTARGETDESC_MERA400FLOATFORMAT_H

#include <cstdint>

namespace llvm {
namespace Mera400 {

enum class FloatConvStatus {
  Ok,
  Overflow,  // too large for the 8-bit exponent, or an IEEE infinity
  Underflow, // too small for the 8-bit exponent; Words are left unspecified
  NotANumber // an IEEE NaN, which the native format cannot represent
};

// Converts F to the native format. With RoundToNearest the mantissa is
// rounded to the nearest representable value, ties to even (what the compiler
// wants); with it off the extra low bits are simply dropped, which is how the
// EMAWP library and EMAS behave (the assembler wants that, to give the same
// words as EMAS). Words[0] holds the most significant word.
FloatConvStatus convertDoubleToNativeFloat(double F, bool RoundToNearest,
                                           uint16_t Words[3]);

} // namespace Mera400
} // namespace llvm

#endif
