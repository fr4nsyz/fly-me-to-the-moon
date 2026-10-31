add_library(sdc_runtime
    runtime.cpp
)

target_include_directories(sdc_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sdc_runtime PUBLIC sdc_core)
