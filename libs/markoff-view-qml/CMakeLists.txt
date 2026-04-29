cmake_minimum_required(VERSION 3.19)
project(markoff_view_qml VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 Test)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_qml_module(markoff_view_qml
    URI org.markoff.view.qml
    VERSION 1.0
    STATIC
    SOURCES
        src/EditorBackend.cpp
        include/markoff/view/qml/EditorBackend.h
        src/SourceTextDocumentBinding.cpp
        include/markoff/view/qml/SourceTextDocumentBinding.h
        src/SearchBackend.cpp
        include/markoff/view/qml/SearchBackend.h
        src/CompletionPopupModel.cpp
        include/markoff/view/qml/CompletionPopupModel.h
        src/LiveSelectionModel.cpp
        include/markoff/view/qml/LiveSelectionModel.h
        src/LiveBlockModel.cpp
        include/markoff/view/qml/LiveBlockModel.h
        src/BlockKind.cpp
        include/markoff/view/qml/BlockKind.h
        include/markoff/view/qml/BlockRecord.h
        src/BlockWalker.cpp
        src/BlockWalker.h
        src/AstBlockDiff.cpp
        src/AstBlockDiff.h
    QML_FILES
        qml/SourceEditor.qml
        qml/SearchBar.qml
        qml/CompletionPopup.qml
        qml/MarkoffEditor.qml
)

target_include_directories(markoff_view_qml
    PUBLIC include/
    # Qt's QML type-registration generator emits `#include <EditorBackend.h>`
    # (bare filename). Adding the leaf directory privately ensures that include
    # resolves without polluting the public include interface.
    PRIVATE include/markoff/view/qml/
)

target_link_libraries(markoff_view_qml PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    markoff_foundation
)

add_subdirectory(app)

# Tests.
if(NOT DEFINED MARKOFF_VIEW_QML_BUILD_TESTS)
    set(MARKOFF_VIEW_QML_BUILD_TESTS ON)
endif()
if(MARKOFF_VIEW_QML_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
