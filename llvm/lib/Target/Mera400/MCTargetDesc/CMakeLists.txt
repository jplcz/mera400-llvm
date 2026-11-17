add_llvm_component_library(LLVMMera400Desc
  Mera400AsmBackend.cpp
  Mera400FloatFormat.cpp
  Mera400InstPrinter.cpp
  Mera400MCCodeEmitter.cpp
  Mera400MCExpr.cpp
  Mera400MCTargetDesc.cpp
  Mera400TargetStreamer.cpp

  LINK_COMPONENTS
  MC
  Mera400Info
  Support

  ADD_TO_COMPONENT
  Mera400
)
