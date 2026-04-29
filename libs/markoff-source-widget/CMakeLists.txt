cmake_minimum_required(VERSION 3.19)
project(markoff_source_widget LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(markoff_source_widget STATIC
    src/Editor.cpp
    src/FindBar.cpp
    src/Gutter.cpp
    include/markoff/source/widget/Editor.h
    include/markoff/source/widget/FindBar.h
    src/Gutter.h
)

target_include_directories(markoff_source_widget
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(markoff_source_widget
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
            KF6::SyntaxHighlighting
            markoff_foundation
)

add_library(Markoff::SourceWidget ALIAS markoff_source_widget)

if(NOT DEFINED MARKOFF_SOURCE_WIDGET_BUILD_TESTS)
    set(MARKOFF_SOURCE_WIDGET_BUILD_TESTS ON)
endif()
if(MARKOFF_SOURCE_WIDGET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
