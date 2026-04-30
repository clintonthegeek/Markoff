cmake_minimum_required(VERSION 3.19)
project(markoff_bench VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

# Bench is a STATIC library used only by the in-tree bench frontends and
# tests. It is not installed and is not part of the public Markoff API.
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
)

target_include_directories(markoff_bench
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../markoff-foundation/src
)

target_link_libraries(markoff_bench
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        markoff_foundation
        MarkoffParser::MarkoffParser
)

# Compile-time path to fixture directory (used by FixtureLoader).
target_compile_definitions(markoff_bench
    PRIVATE
        MARKOFF_BENCH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_library(Markoff::Bench ALIAS markoff_bench)

# Tests.
if(NOT DEFINED MARKOFF_BENCH_BUILD_TESTS)
    set(MARKOFF_BENCH_BUILD_TESTS ON)
endif()
if(MARKOFF_BENCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
