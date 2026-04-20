cmake_minimum_required(VERSION 3.19)
project(markoff_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

add_library(markoff_core STATIC
    src/EphemeralState.cpp
    src/MarkoffDocument.cpp
    include/markoff/CursorPos.h
    include/markoff/TextSpan.h
    include/markoff/FoldSpec.h
    include/markoff/EphemeralState.h
    include/markoff/MarkoffDocument.h
    include/markoff/MarkoffCoreExport.h
)

target_include_directories(markoff_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(markoff_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        KF6::SyntaxHighlighting
        MarkoffParser::MarkoffParser
)

set_target_properties(markoff_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Markoff::Core ALIAS markoff_core)

option(MARKOFF_CORE_BUILD_TESTS "Build markoff-core tests" ON)
if(MARKOFF_CORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
