add_llvm_component_library(LLVMMera400AsmParser
  Mera400AsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  Mera400Desc
  Mera400Info
  Support

  ADD_TO_COMPONENT
  Mera400
)
