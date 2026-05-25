cmake_minimum_required(VERSION 3.19)
project(cross_block_selection_spike CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(spike
    main.cpp
    SelectionModel.h
    SelectionModel.cpp
)

qt_add_qml_module(spike
    URI CrossBlockSpike
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES SelectionModel.h SelectionModel.cpp
)

target_link_libraries(spike PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)
