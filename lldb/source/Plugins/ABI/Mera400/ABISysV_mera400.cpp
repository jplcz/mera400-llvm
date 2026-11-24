//===-- ABISysV_mera400.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_mera400.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/ADT/bit.h"
#include <cmath>
#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_mera400, ABIMera400)

// DWARF register numbers; they must match DwarfNumbers in Mera400.td.
namespace {
enum DwarfRegNum : uint32_t {
  dwarf_r4 = 4,
  dwarf_r5 = 5,
  dwarf_r6 = 6,
  dwarf_r7 = 7,
  dwarf_ic = 8,
};

// DWARF expression "R4 << 1": converts the word address left in R4 by `rj`
// into the byte address LLDB uses (DW_OP_breg4 0; DW_OP_lit1; DW_OP_shl).
const uint8_t g_ic_from_r4[] = {0x74, 0x00, 0x31, 0x24};
} // namespace

ABISP ABISysV_mera400::CreateInstance(ProcessSP process_sp,
                                      const ArchSpec &arch) {
  if (arch.GetTriple().getArch() != llvm::Triple::mera400)
    return ABISP();
  return ABISP(
      new ABISysV_mera400(std::move(process_sp), MakeMCRegisterInfo(arch)));
}

namespace {
// A floating point value occupies three registers (or stack words): a 40-bit
// two's complement mantissa followed by an 8-bit exponent; see
// llvm/lib/Target/Mera400/MCTargetDesc/Mera400FloatFormat.h. LLDB describes
// the type as an IEEE double, so values are converted at the boundary.
double NativeToDouble(const uint16_t words[3]) {
  const int64_t mantissa = static_cast<int64_t>(
      (uint64_t(words[0]) << 48) | (uint64_t(words[1]) << 32) |
      (uint64_t(words[2] & 0xff00) << 16));
  const int8_t exponent = static_cast<int8_t>(words[2] & 0xff);
  return std::ldexp(static_cast<double>(mantissa), exponent - 63);
}

// Converts to the native format, rounding to nearest like the compiler does.
// Returns false for values the format cannot hold (NaN, infinity, too large or
// too small).
bool DoubleToNative(double value, uint16_t words[3]) {
  constexpr uint64_t sign_bit = 1ULL << 63;
  constexpr uint64_t dropped_bits = 0xffffffULL; // the 40-bit mantissa
  auto normalised = [](uint64_t m) { return ((m ^ (m << 1)) & sign_bit) != 0; };

  if (std::isnan(value) || std::isinf(value))
    return false;
  int exponent;
  const double fraction = std::frexp(value, &exponent);
  uint64_t m = static_cast<uint64_t>(
      static_cast<int64_t>(std::ldexp(fraction, 63)));
  auto normalise = [&] {
    while (m != 0 && !normalised(m)) {
      m <<= 1;
      --exponent;
    }
  };
  normalise();
  if (m != 0) {
    const uint64_t dropped = m & dropped_bits;
    const bool odd = (m & (dropped_bits + 1)) != 0;
    const bool was_positive = !(m & sign_bit);
    m &= ~dropped_bits;
    if (dropped > 0x800000ULL || (dropped == 0x800000ULL && odd))
      m += dropped_bits + 1;
    if (was_positive && (m & sign_bit)) {
      m = sign_bit >> 1;
      ++exponent;
    }
    normalise();
  } else {
    exponent = 0;
  }
  if (exponent > 127 || exponent < -128)
    return false;
  words[0] = m >> 48;
  words[1] = m >> 32;
  words[2] = ((m >> 16) & 0xff00) | (exponent & 0xff);
  return true;
}

// Where an argument lives at a call: in registers R1..R3 starting at `reg`
// (0 for R1), or on the stack at word `stack_word` above the caller's R7.
struct ArgLocation {
  bool in_registers;
  unsigned index;
};

// Mirrors analyzeArgs() in Mera400ISelLowering.cpp: a value of `words` words
// goes to registers if all of its words fit in the ones that are left, and
// otherwise (never torn between registers and memory) to the stack, which
// also uses up the remaining registers.
ArgLocation AllocateArgument(unsigned words, unsigned &next_reg,
                             unsigned &next_stack_word) {
  if (next_reg + words <= 3) {
    ArgLocation loc = {true, next_reg};
    next_reg += words;
    return loc;
  }
  next_reg = 3;
  ArgLocation loc = {false, next_stack_word};
  next_stack_word += words;
  return loc;
}

constexpr const char *g_arg_regs[] = {"r1", "r2", "r3"};

bool IsScalarArgument(CompilerType type, size_t &byte_size, bool &is_float,
                      bool &is_signed, Thread &thread) {
  const uint32_t flags = type.GetTypeInfo();
  byte_size =
      llvm::expectedToOptional(type.GetByteSize(&thread)).value_or(0);
  is_float = flags & eTypeIsFloat;
  is_signed = false;
  if (is_float)
    return byte_size == 8;
  if (!(flags & (eTypeIsInteger | eTypeIsPointer | eTypeIsEnumeration)))
    return false;
  type.IsIntegerOrEnumerationType(is_signed);
  return byte_size >= 1 && byte_size <= 4;
}
} // namespace

bool ABISysV_mera400::PrepareTrivialCall(Thread &thread, addr_t sp,
                                         addr_t functionAddress,
                                         addr_t returnAddress,
                                         llvm::ArrayRef<addr_t> args) const {
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;
  // Every argument is one word, so it must fit in 16 bits; the return address
  // is loaded into R4 as a word address.
  if ((returnAddress & 1) || (functionAddress & 1))
    return false;
  for (addr_t arg : args)
    if (arg > 0xffff)
      return false;

  const size_t stack_words = args.size() > 3 ? args.size() - 3 : 0;
  sp &= ~addr_t(1);
  sp -= 2 * stack_words;

  // The first stack argument is at the lowest address, which is R7 at the
  // call.
  Status error;
  for (size_t i = 3; i < args.size(); ++i) {
    const uint8_t word[2] = {uint8_t(args[i] >> 8), uint8_t(args[i])};
    if (thread.GetProcess()->WriteMemory(sp + 2 * (i - 3), word, 2, error) !=
            2 ||
        error.Fail())
      return false;
  }

  for (size_t i = 0; i < args.size() && i < 3; ++i)
    if (!reg_ctx->WriteRegisterFromUnsigned(
            reg_ctx->GetRegisterInfoByName(g_arg_regs[i]), args[i]))
      return false;

  return reg_ctx->WriteRegisterFromUnsigned(
             reg_ctx->GetRegisterInfoByName("r4"), returnAddress / 2) &&
         reg_ctx->WriteRegisterFromUnsigned(
             reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                      LLDB_REGNUM_GENERIC_SP),
             sp) &&
         reg_ctx->WriteRegisterFromUnsigned(
             reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                      LLDB_REGNUM_GENERIC_PC),
             functionAddress);
}

// Reads the arguments of the function a thread is stopped at the entry of.
bool ABISysV_mera400::GetArgumentValues(Thread &thread,
                                        ValueList &values) const {
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;
  const addr_t sp = reg_ctx->GetSP(0);
  Process &process = *thread.GetProcess();

  unsigned next_reg = 0, next_stack_word = 0;
  for (uint32_t i = 0, e = values.GetSize(); i != e; ++i) {
    Value *value = values.GetValueAtIndex(i);
    if (!value)
      return false;
    CompilerType type = value->GetCompilerType();
    size_t byte_size;
    bool is_float, is_signed;
    if (!type || !IsScalarArgument(type, byte_size, is_float, is_signed, thread))
      return false;

    const unsigned words = is_float ? 3 : (byte_size + 1) / 2;
    const ArgLocation loc =
        AllocateArgument(words, next_reg, next_stack_word);

    // Collect the words, most significant first.
    uint16_t parts[3] = {0, 0, 0};
    for (unsigned w = 0; w < words; ++w) {
      if (loc.in_registers) {
        parts[w] = reg_ctx->ReadRegisterAsUnsigned(
            reg_ctx->GetRegisterInfoByName(g_arg_regs[loc.index + w]), 0);
      } else {
        Status error;
        parts[w] = process.ReadUnsignedIntegerFromMemory(
            sp + 2 * (loc.index + w), 2, 0, error);
        if (error.Fail())
          return false;
      }
    }

    if (is_float) {
      value->GetScalar() = Scalar(NativeToDouble(parts));
      continue;
    }
    uint64_t number = words == 2 ? (uint64_t(parts[0]) << 16) | parts[1]
                                 : parts[0];
    if (byte_size == 1)
      number &= 0xff;
    if (is_signed)
      value->GetScalar() = Scalar(llvm::SignExtend64(number, byte_size * 8));
    else
      value->GetScalar() = Scalar(number);
  }
  return true;
}

Status ABISysV_mera400::SetReturnValueObject(StackFrameSP &frame_sp,
                                             ValueObjectSP &new_value_sp) {
  if (!new_value_sp)
    return Status::FromErrorString("empty value object");
  if (!frame_sp)
    return Status::FromErrorString("no frame");
  RegisterContext *reg_ctx = frame_sp->GetThread()->GetRegisterContext().get();
  if (!reg_ctx)
    return Status::FromErrorString("no register context");

  size_t byte_size;
  bool is_float, is_signed;
  if (!IsScalarArgument(new_value_sp->GetCompilerType(), byte_size, is_float,
                        is_signed, *frame_sp->GetThread()))
    return Status::FromErrorString(
        "only integers, pointers, enumerations and floating point values can "
        "be returned");

  uint16_t parts[3] = {0, 0, 0};
  unsigned words;
  if (is_float) {
    Status error;
    DataExtractor data;
    new_value_sp->GetData(data, error);
    if (error.Fail())
      return error;
    lldb::offset_t offset = 0;
    if (!DoubleToNative(data.GetDouble(&offset), parts))
      return Status::FromErrorString(
          "the value cannot be represented in the MERA-400 floating point "
          "format");
    words = 3;
  } else {
    const uint64_t number = new_value_sp->GetValueAsUnsigned(0);
    words = byte_size == 4 ? 2 : 1;
    if (words == 2) {
      parts[0] = number >> 16;
      parts[1] = number;
    } else {
      parts[0] = number;
    }
  }

  for (unsigned w = 0; w < words; ++w)
    if (!reg_ctx->WriteRegisterFromUnsigned(
            reg_ctx->GetRegisterInfoByName(g_arg_regs[w]), parts[w]))
      return Status::FromErrorString("failed to write a return register");
  return Status();
}

ValueObjectSP
ABISysV_mera400::GetReturnValueObjectImpl(Thread &thread,
                                          CompilerType &return_type) const {
  if (!return_type)
    return ValueObjectSP();

  RegisterContextSP reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return ValueObjectSP();

  size_t byte_size;
  bool is_float, is_signed;
  // Aggregates and 64-bit integers come back through a hidden pointer that
  // the callee does not return, so they cannot be recovered afterwards.
  if (!IsScalarArgument(return_type, byte_size, is_float, is_signed, thread))
    return ValueObjectSP();

  uint16_t parts[3];
  for (unsigned w = 0; w < 3; ++w)
    parts[w] = reg_ctx->ReadRegisterAsUnsigned(
                   reg_ctx->GetRegisterInfoByName(g_arg_regs[w]), 0) &
               0xffff;

  // Lay the value out as the target would keep it in memory: big-endian.
  // (A float is shown as the IEEE double LLDB believes the type to be.)
  auto buffer_sp = std::make_shared<DataBufferHeap>(byte_size, 0);
  uint8_t *bytes = buffer_sp->GetBytes();
  if (is_float) {
    uint64_t bits = llvm::bit_cast<uint64_t>(NativeToDouble(parts));
    for (size_t i = 0; i < 8; ++i)
      bytes[i] = static_cast<uint8_t>(bits >> (8 * (7 - i)));
  } else {
    // R1 is the high word of a 32-bit result; a 16-bit result is just R1.
    const uint64_t value =
        byte_size == 4 ? (uint64_t(parts[0]) << 16) | parts[1] : parts[0];
    for (size_t i = 0; i < byte_size; ++i)
      bytes[i] = static_cast<uint8_t>(value >> (8 * (byte_size - 1 - i)));
  }

  return ValueObjectConstResult::Create(
      thread.GetStackFrameAtIndex(0).get(), return_type, ConstString(""),
      buffer_sp, eByteOrderBig, GetProcessSP()->GetAddressByteSize());
}

// Called on the first instruction of a function: nothing was pushed yet, the
// return address is still in R4 and the stack pointer is the CFA.
UnwindPlanSP ABISysV_mera400::CreateFunctionEntryUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_r7, 0);
  row.SetRegisterLocationToIsDWARFExpression(dwarf_ic, g_ic_from_r4,
                                             sizeof(g_ic_from_r4), true);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf_r7, 0, true);
  // Caller-saved registers cannot be recovered; R5 and R6 are callee-saved.
  row.SetUnspecifiedRegistersAreUndefined(true);
  row.SetRegisterLocationToSame(dwarf_r5, false);
  row.SetRegisterLocationToSame(dwarf_r6, false);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("mera400 at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  return plan_sp;
}

// Used when a function has no call frame information. Without a frame pointer
// the layout of the stack is unknown in the middle of a function, so this is
// right only for leaf functions that have not touched the stack pointer.
UnwindPlanSP ABISysV_mera400::CreateDefaultUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_r7, 0);
  row.SetRegisterLocationToIsDWARFExpression(dwarf_ic, g_ic_from_r4,
                                             sizeof(g_ic_from_r4), true);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf_r7, 0, true);
  // Caller-saved registers cannot be recovered; R5 and R6 are callee-saved.
  row.SetUnspecifiedRegistersAreUndefined(true);
  row.SetRegisterLocationToSame(dwarf_r5, false);
  row.SetRegisterLocationToSame(dwarf_r6, false);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("mera400 default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return plan_sp;
}

bool ABISysV_mera400::RegisterIsVolatile(const RegisterInfo *reg_info) {
  if (!reg_info)
    return false;
  // R5 and R6 survive calls; so do the stack pointer and the instruction
  // counter (restored by the unwinder from the CFA and R4).
  return !llvm::StringSwitch<bool>(reg_info->name)
              .Cases({"r5", "r6", "r7", "ic"}, true)
              .Default(false);
}

uint32_t ABISysV_mera400::GetGenericNum(llvm::StringRef name) {
  return llvm::StringSwitch<uint32_t>(name)
      .Case("ic", LLDB_REGNUM_GENERIC_PC)
      .Case("r7", LLDB_REGNUM_GENERIC_SP)
      .Case("r1", LLDB_REGNUM_GENERIC_ARG1)
      .Case("r2", LLDB_REGNUM_GENERIC_ARG2)
      .Case("r3", LLDB_REGNUM_GENERIC_ARG3)
      .Default(LLDB_INVALID_REGNUM);
}

void ABISysV_mera400::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "System V-like ABI for mera400 targets",
                                CreateInstance);
}

void ABISysV_mera400::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
