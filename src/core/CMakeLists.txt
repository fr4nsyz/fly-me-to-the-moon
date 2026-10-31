add_library(sdc_core
    trace_writer.cpp
    trace_reader.cpp
    op_kind.cpp
)

target_include_directories(sdc_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
