add_executable(test_trace_io test_trace_io.cpp)
target_link_libraries(test_trace_io PRIVATE sdc_core)

add_executable(test_analyzer test_analyzer.cpp)
target_include_directories(test_analyzer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/analyzer
)
target_link_libraries(test_analyzer PRIVATE sdc_analyzer_core)

enable_testing()
add_test(NAME trace_io COMMAND test_trace_io)
add_test(NAME analyzer COMMAND test_analyzer)
