add_llvm_component_group(Mera400)

set(LLVM_TARGET_DEFINITIONS Mera400.td)
llvm_tablegen(Mera400GenAsmMatcher.inc -gen-asm-matcher)
llvm_tablegen(Mera400GenAsmWriter.inc -gen-asm-writer)
llvm_tablegen(Mera400GenCallingConv.inc -gen-callingconv)
llvm_tablegen(Mera400GenDAGISel.inc -gen-dag-isel)
llvm_tablegen(Mera400GenInstrInfo.inc -gen-instr-info)
llvm_tablegen(Mera400GenMCCodeEmitter.inc -gen-emitter)
llvm_tablegen(Mera400GenRegisterInfo.inc -gen-register-info)
llvm_tablegen(Mera400GenSDNodeInfo.inc -gen-sd-node-info)
llvm_tablegen(Mera400GenSubtargetInfo.inc -gen-subtarget)
add_public_tablegen_target(Mera400CommonTableGen)

add_llvm_target(Mera400CodeGen
  Mera400AsmPrinter.cpp
  Mera400CodeGenPassBuilder.cpp
  Mera400FrameLowering.cpp
  Mera400ISelDAGToDAG.cpp
  Mera400ISelLowering.cpp
  Mera400InstrInfo.cpp
  Mera400LowerGlobalSpaces.cpp
  Mera400LowerPointers.cpp
  Mera400MCInstLower.cpp
  Mera400PunnedFloats.cpp
  Mera400RegisterInfo.cpp
  Mera400SelectionDAGInfo.cpp
  Mera400Subtarget.cpp
  Mera400Target.cpp
  Mera400TargetMachine.cpp

  LINK_COMPONENTS
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  Mera400Desc
  Mera400Info
  Passes
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  Mera400
)

add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
