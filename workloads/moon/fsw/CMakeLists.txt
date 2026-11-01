# Plain build for unit tests; the instrumented build is tools/instrument.sh --profile o0.
add_library(moon_fsw STATIC navigation.cpp)
target_include_directories(moon_fsw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/workloads/moon/common)
