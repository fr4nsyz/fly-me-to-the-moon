if(LLVM_FOUND)
  include(AddLLVM)
  add_llvm_pass_plugin(SDCInstrument
      SDCInstrument.cpp
  )

  target_include_directories(SDCInstrument PUBLIC
      ${CMAKE_SOURCE_DIR}/include
  )
endif()
