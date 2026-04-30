cmake_minimum_required(VERSION 3.19)
project(markoff_bench_apps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

add_executable(markoff-bench-parse markoff-bench-parse.cpp)
target_link_libraries(markoff-bench-parse PRIVATE
    Qt6::Core
    markoff_bench
)

# Tier 2 binary added in Task 14.
