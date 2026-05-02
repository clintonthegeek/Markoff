cmake_minimum_required(VERSION 3.19)
project(markoff_live_render VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Widgets Quick QuickControls2 Qml Test)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_qml_module(markoff_live_render
    URI org.markoff.live.render
    VERSION 1.0
    STATIC
    SOURCES
        src/Version.cpp
        include/markoff/live-render/MarkoffLiveRenderExport.h
        include/markoff/live-render/Version.h
    QML_FILES
        qml/Placeholder.qml
)

target_include_directories(markoff_live_render
    PUBLIC include/
    # Bare-filename `#include <Version.h>` resolution for Qt's QML
    # type-registration generated code, mirroring markoff-view-qml's
    # private include directory pattern.
    PRIVATE include/markoff/live-render/
)

target_link_libraries(markoff_live_render PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    markoff_foundation
)

add_subdirectory(app)

if(NOT DEFINED MARKOFF_LIVE_RENDER_BUILD_TESTS)
    set(MARKOFF_LIVE_RENDER_BUILD_TESTS ON)
endif()
if(MARKOFF_LIVE_RENDER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
