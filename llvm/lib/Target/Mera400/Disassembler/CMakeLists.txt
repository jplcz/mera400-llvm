add_llvm_component_library(LLVMMera400Disassembler
  Mera400Disassembler.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  Mera400Desc
  Mera400Info
  Support

  ADD_TO_COMPONENT
  Mera400
)
