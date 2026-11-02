# Plain build for unit tests; the instrumented build is tools/instrument.sh --replicate.
add_library(moon_fsw STATIC navigation.cpp guidance.cpp redundancy.cpp)
target_include_directories(moon_fsw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/workloads/moon/common)
target_compile_options(moon_fsw PRIVATE -ffp-contract=off)
