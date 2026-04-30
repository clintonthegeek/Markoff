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

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick Widgets QuickControls2)

add_executable(markoff-bench-render markoff-bench-render.cpp)
target_link_libraries(markoff-bench-render PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    markoff_bench
    markoff_view_qml
    markoff_view_qmlplugin
    markoff_foundation
)
qt6_import_qml_plugins(markoff-bench-render)
