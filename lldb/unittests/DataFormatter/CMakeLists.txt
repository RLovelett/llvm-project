add_lldb_unittest(LLDBFormatterTests
  FormatManagerTests.cpp

  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginPlatformMacOSX

  LINK_COMPONENTS
    Support
  )
