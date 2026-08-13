cmake_minimum_required(VERSION 3.19)
project(markoff_canvas LANGUAGES CXX)

# markoff-canvas — projection view leaf (SPIKE).
#
# Constitution C3 (spec §6): this target links Qt6::Widgets, markoff_core
# and Markoff::Parser and NOTHING ELSE from the Markoff family. No
# Qt6::Quick / Qt6::QuickWidgets, no markoff_live / markoff_styled /
# markoff_source. Adding one of those to the link line below is a
# constitution violation, not a build fix.

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(markoff_canvas STATIC
    include/markoff/canvas/View.h
    src/View.cpp
    src/BlockLayoutCache.h
    src/BlockLayoutCache.cpp
    src/BlockPresentation.h
    src/BlockPresentation.cpp
    src/Coordinates.h
    src/Coordinates.cpp
    src/InlineFormatting.h
    src/InlineFormatting.cpp
    src/InputPredicate.h
    src/InputPredicate.cpp
    src/KindTransition.h
    src/KindTransition.cpp
    src/TableGeometry.h
    src/TableGeometry.cpp
)

target_include_directories(markoff_canvas
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(markoff_canvas
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
            markoff_core
            Markoff::Parser
)

add_library(Markoff::Canvas ALIAS markoff_canvas)

if(NOT DEFINED MARKOFF_CANVAS_BUILD_TESTS)
    set(MARKOFF_CANVAS_BUILD_TESTS ON)
endif()
if(MARKOFF_CANVAS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(app)
