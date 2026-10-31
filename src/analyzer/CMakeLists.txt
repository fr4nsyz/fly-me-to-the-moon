# The analyzer's deterministic core, as a library so the tests can link it.
add_library(sdc_analyzer_core
    align.cpp
    audit.cpp
    classify.cpp
    evidence.cpp
    ops_map.cpp
)

target_include_directories(sdc_analyzer_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sdc_analyzer_core PUBLIC sdc_core)

add_executable(sdc-analyze main.cpp)
target_link_libraries(sdc-analyze PRIVATE sdc_analyzer_core)
