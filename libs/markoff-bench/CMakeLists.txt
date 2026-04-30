cmake_minimum_required(VERSION 3.19)
project(markoff_bench VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test)

# Bench is a STATIC library used only by the in-tree bench frontends and
# tests. It is not installed and is not part of the public Markoff API.
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
    src/AllocCounter.cpp
    src/AllocShim.cpp
    src/CorpusGen.cpp
    src/FixtureLoader.cpp
    src/Scenario.cpp
    src/ScenarioRunner.cpp
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
        Qt6::Test
        markoff_foundation
        MarkoffParser::MarkoffParser
)

# Compile-time path to fixture directory (used by FixtureLoader).
target_compile_definitions(markoff_bench
    PRIVATE
        MARKOFF_BENCH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_library(Markoff::Bench ALIAS markoff_bench)

# Export operator new/delete from consuming binaries so libstdc++.so's
# internal allocation calls resolve to our replacement (not its own).
# Only meaningful on ELF/Linux; harmless elsewhere because the linker
# silently ignores unrecognized -rdynamic on platforms that lack it.
if(UNIX AND NOT APPLE)
    target_link_options(markoff_bench INTERFACE -rdynamic)
endif()

# Prevent GCC's allocation-DCE pass from eliding new/delete pairs at
# call sites in bench consumers.  Without this flag, GCC -O2 proves
# that a local std::vector::reserve() or new[]/delete[] whose result
# does not escape has no observable effect and removes the call
# entirely — making the AllocShim never fire.  -fno-allocation-dce
# restores the call so our operator new replacement is actually reached.
# The flag is GCC-specific; Clang and MSVC don't need it (they don't
# perform this optimisation) and silently ignore unknown -f flags, so
# the guard is only on the compiler family.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(markoff_bench INTERFACE -fno-allocation-dce)
endif()

# Tests.
if(NOT DEFINED MARKOFF_BENCH_BUILD_TESTS)
    set(MARKOFF_BENCH_BUILD_TESTS ON)
endif()
if(MARKOFF_BENCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
