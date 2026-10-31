add_library(moon_plant STATIC dynamics.cpp)
target_include_directories(moon_plant PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/workloads/moon/common
)
