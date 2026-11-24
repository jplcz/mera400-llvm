//===-- ABISysV_mera400.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_MERA400_ABISYSV_MERA400_H
#define LLDB_SOURCE_PLUGINS_ABI_MERA400_ABISYSV_MERA400_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

/// The calling convention of the MERA-400 as implemented by the LLVM back end
/// (llvm/lib/Target/Mera400/Mera400CallingConv.td).
///
/// The debugger sees addresses as byte addresses, although the machine counts
/// 16-bit words. R7 (the stack pointer) is therefore reported in bytes, while
/// R4 (the return address, written by the `rj` instruction) is a word address:
/// the unwind plans multiply it by two to produce the caller's IC (the
/// instruction counter, which LLDB treats as the PC).
///
/// Arguments and results travel in R1, R2 and R3, 32-bit values high word
/// first; a floating point value is three words (R1..R3). Aggregates and 64-bit
/// integers are returned through a hidden pointer that the callee does not
/// hand back, so `finish` cannot show them. Callers can rely on R5 and R6 surviving a call; R1..R4 do not.
class ABISysV_mera400 : public lldb_private::MCBasedABI {
public:
  ~ABISysV_mera400() override = default;

  size_t GetRedZoneSize() const override { return 0; }

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override;

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override;

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  lldb::UnwindPlanSP CreateFunctionEntryUnwindPlan() override;

  lldb::UnwindPlanSP CreateDefaultUnwindPlan() override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    // The stack grows in whole words and address 0 is never a frame.
    return cfa != 0 && (cfa & 1) == 0;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override {
    // Instructions are made of 16-bit words, so code addresses are even.
    return (pc & 1) == 0;
  }

  // The whole address space is 128 KiB, so the default 512 KiB is useless.
  uint64_t GetStackFrameSize() override { return 4096; }

  static void Initialize();

  static void Terminate();

  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);

  static llvm::StringRef GetPluginNameStatic() { return "sysv-mera400"; }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  uint32_t GetGenericNum(llvm::StringRef name) override;

  using lldb_private::MCBasedABI::MCBasedABI;
};

#endif // LLDB_SOURCE_PLUGINS_ABI_MERA400_ABISYSV_MERA400_H
