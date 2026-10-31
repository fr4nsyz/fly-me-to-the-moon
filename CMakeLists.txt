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
