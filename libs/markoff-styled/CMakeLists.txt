cmake_minimum_required(VERSION 3.19)
project(markoff_styled LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(markoff_styled STATIC
    include/markoff/styled/Editor.h
    include/markoff/styled/MarkoffStyledExport.h
    src/Editor.cpp
    src/StyleApplier.h
    src/StyleApplier.cpp
)

target_include_directories(markoff_styled
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(markoff_styled
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
            markoff_core
)

add_library(Markoff::Styled ALIAS markoff_styled)

if(NOT DEFINED MARKOFF_STYLED_BUILD_TESTS)
    set(MARKOFF_STYLED_BUILD_TESTS ON)
endif()
if(MARKOFF_STYLED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(app)
