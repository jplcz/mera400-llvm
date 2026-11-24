add_lldb_library(lldbPluginABIMera400 PLUGIN
  ABISysV_mera400.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
