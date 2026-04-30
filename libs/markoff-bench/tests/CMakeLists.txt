cmake_minimum_required(VERSION 3.19)
project(markoff_bench_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Tests are added incrementally as tasks land.

add_executable(tst_bench_phase_timer tst_bench_phase_timer.cpp)
add_test(NAME tst_bench_phase_timer COMMAND tst_bench_phase_timer)
target_link_libraries(tst_bench_phase_timer PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_phase_timer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
