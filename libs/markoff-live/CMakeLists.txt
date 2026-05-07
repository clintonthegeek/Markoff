cmake_minimum_required(VERSION 3.19)
project(markoff_live VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Widgets Quick QuickControls2 Qml Test)

qt_standard_project_setup(REQUIRES 6.5)

# QTP0004 NEW (Qt 6.8+): every directory contributing .qml files to a
# module is expected to carry its own qmldir. We add qmldir files under
# each library's qml/delegates/ subdirectory listing each delegate by
# logical type name. Gated on QT_KNOWN_POLICY_QTP0004 for forward
# compatibility with Qt < 6.8 envs. See
# docs/2026-05-03-cmake-warnings-rationale.md.
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_qml_module(markoff_live
    URI org.markoff.live
    VERSION 1.0
    STATIC
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/org/markoff/live
    SOURCES
        src/Version.cpp
        include/markoff/live/MarkoffLiveExport.h
        include/markoff/live/Version.h
        # Block value types
        include/markoff/live/BlockRecord.h
        include/markoff/live/BlockKind.h
        include/markoff/live/BlockKindDescriptor.h
        include/markoff/live/BlockKindRegistry.h
        include/markoff/live/Coordinates.h
        include/markoff/live/AstBlockDiff.h
        include/markoff/live/LiveBlockModel.h
        include/markoff/live/LiveListModelBinding.h
        src/BlockKind.cpp
        src/BlockKindRegistry.cpp
        src/Coordinates.cpp
        src/AstBlockDiff.cpp
        src/KindTransition.h
        src/KindTransition.cpp
        src/LiveBlockModel.cpp
        src/LiveListModelBinding.cpp
        # R3: cursor + selection
        include/markoff/live/Cursor.h
        include/markoff/live/LiveCursorState.h
        include/markoff/live/BlockHitTester.h
        include/markoff/live/LiveSelectionView.h
        src/LiveCursorState.cpp
        src/BlockHitTester.cpp
        src/LiveSelectionView.cpp
        # R4: per-delegate edit binding
        include/markoff/live/LiveEditBinding.h
        src/LiveEditBinding.cpp
        # R5: structural keys
        include/markoff/live/LiveStructuralKeyHandler.h
        src/LiveStructuralKeyHandler.cpp
        # Math rendering
        src/MathRenderer.h
        src/MathRenderer.cpp
    QML_FILES
        qml/LiveView.qml
        qml/LiveContextMenu.qml
        qml/delegates/ParagraphDelegate.qml
        qml/delegates/HeadingDelegate.qml
        qml/delegates/CodeBlockDelegate.qml
        qml/delegates/HorizontalRuleDelegate.qml
        qml/delegates/ImageDelegate.qml
        qml/delegates/ListItemDelegate.qml
        qml/delegates/BlockquoteDelegate.qml
        qml/delegates/MathDelegate.qml
)

target_include_directories(markoff_live
    PUBLIC include/
    PRIVATE include/markoff/live/
    PRIVATE src/
)

target_link_libraries(markoff_live PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    markoff_core
    jkqtmathtext
)

add_subdirectory(app)

if(NOT DEFINED MARKOFF_LIVE_BUILD_TESTS)
    set(MARKOFF_LIVE_BUILD_TESTS ON)
endif()
if(MARKOFF_LIVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
