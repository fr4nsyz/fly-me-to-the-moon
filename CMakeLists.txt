cmake_minimum_required(VERSION 3.20)
project(sdc-investigator LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG HINTS /opt/homebrew/opt/llvm/lib/cmake/llvm)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(src/instrumentation)
add_subdirectory(src/runtime)
add_subdirectory(src/core)
add_subdirectory(src/analyzer)
add_subdirectory(workloads/moon/plant)
add_subdirectory(workloads/moon/fsw)

option(SDC_BUILD_TESTS "Build unit tests (requires GoogleTest)" ON)
if(SDC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
endif()

add_executable(sum workloads/reductions/sum.cpp)
target_link_libraries(sum PRIVATE sdc_runtime)

if(LLVM_FOUND)
  include(AddLLVM)
endif()
