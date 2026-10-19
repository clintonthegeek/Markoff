cmake_minimum_required(VERSION 3.19)
# ctest selection labels. The whole suite here is fast and self-checking;
# the manual load benchmark (perf_load_bench) deliberately stays out of ctest
# because it needs an input file and prints numbers instead of asserting.
set_property(DIRECTORY APPEND PROPERTY LABELS unit)

find_package(Qt6 6.8 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tst_embed_runtime tst_embed_runtime.cpp)
add_test(NAME tst_embed_runtime COMMAND tst_embed_runtime)
target_link_libraries(tst_embed_runtime PRIVATE Qt6::Test Qt6::Widgets Markoff::Embed Markoff::Core)
set_tests_properties(tst_embed_runtime PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
