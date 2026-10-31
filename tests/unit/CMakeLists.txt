find_package(GTest 1.18 REQUIRED CONFIG HINTS /opt/homebrew/lib/cmake/GTest)
include(GoogleTest)

add_executable(test_trace_io test_trace_io.cpp)
target_link_libraries(test_trace_io PRIVATE sdc_core GTest::gtest_main)

add_executable(test_analyzer test_analyzer.cpp)
target_include_directories(test_analyzer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/analyzer
)
target_link_libraries(test_analyzer PRIVATE sdc_analyzer_core GTest::gtest_main)

add_executable(test_vec3 test_vec3.cpp)
target_include_directories(test_vec3 PRIVATE ${CMAKE_SOURCE_DIR}/workloads/moon/common)
target_link_libraries(test_vec3 PRIVATE GTest::gtest_main)

add_executable(test_moon_ephemeris test_moon_ephemeris.cpp)
target_include_directories(test_moon_ephemeris PRIVATE ${CMAKE_SOURCE_DIR}/workloads/moon/common)
target_link_libraries(test_moon_ephemeris PRIVATE GTest::gtest_main)

add_executable(test_dynamics test_dynamics.cpp)
target_link_libraries(test_dynamics PRIVATE moon_plant GTest::gtest_main)

gtest_discover_tests(test_trace_io)
gtest_discover_tests(test_analyzer)
gtest_discover_tests(test_vec3)
gtest_discover_tests(test_moon_ephemeris)
gtest_discover_tests(test_dynamics)
