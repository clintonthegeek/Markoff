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

add_executable(tst_bench_percentile tst_bench_percentile.cpp)
add_test(NAME tst_bench_percentile COMMAND tst_bench_percentile)
target_link_libraries(tst_bench_percentile PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_percentile PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_bench_alloc_counter tst_bench_alloc_counter.cpp)
add_test(NAME tst_bench_alloc_counter COMMAND tst_bench_alloc_counter)
target_link_libraries(tst_bench_alloc_counter PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_alloc_counter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_bench_corpus_gen tst_bench_corpus_gen.cpp)
add_test(NAME tst_bench_corpus_gen COMMAND tst_bench_corpus_gen)
target_link_libraries(tst_bench_corpus_gen PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_corpus_gen PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60)

add_executable(tst_bench_fixture_loader tst_bench_fixture_loader.cpp)
add_test(NAME tst_bench_fixture_loader COMMAND tst_bench_fixture_loader)
target_link_libraries(tst_bench_fixture_loader PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_fixture_loader PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_bench_scenario tst_bench_scenario.cpp)
add_test(NAME tst_bench_scenario COMMAND tst_bench_scenario)
target_link_libraries(tst_bench_scenario PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_scenario PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
