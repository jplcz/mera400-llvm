add_llvm_component_library(LLVMMera400Info
  Mera400TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Mera400
)
