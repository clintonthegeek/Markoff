cmake_minimum_required(VERSION 3.19)
project(markoff_live VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Widgets Quick QuickControls2 QuickWidgets Qml Test)
find_package(KF6SyntaxHighlighting REQUIRED)

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
        include/markoff/live/ThemeForeign.h
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
        src/KindDispatch.h
        src/KindDispatch.cpp
        src/LiveBlockModel.cpp
        src/LiveListModelBinding.cpp
        # R3: cursor + selection
        include/markoff/live/Cursor.h
        include/markoff/live/LiveCursorState.h
        include/markoff/live/BlockHitTester.h
        src/LiveCursorState.cpp
        src/BlockHitTester.cpp
        # R4: per-delegate edit binding
        include/markoff/live/LiveEditBinding.h
        src/LiveEditBinding.cpp
        # R5: structural keys
        include/markoff/live/LiveStructuralKeyHandler.h
        src/LiveStructuralKeyHandler.cpp
        # Math rendering
        src/MathRenderer.h
        src/MathRenderer.cpp
        # E1: inline-format highlighter
        include/markoff/live/InlineHighlighter.h
        src/InlineHighlighter.cpp
        include/markoff/live/InlineHighlighterAttached.h
        src/InlineHighlighterAttached.cpp
        # E2: navigation controller
        include/markoff/live/LiveNavigationController.h
        src/LiveNavigationController.cpp
        # E2.5 Phase C: clipboard controller
        include/markoff/live/LiveClipboardController.h
        src/LiveClipboardController.cpp
        # E2.5 Phase D: action controller
        include/markoff/live/LiveActionController.h
        src/LiveActionController.cpp
        # E2.5 Phase E: format controller
        include/markoff/live/LiveFormatController.h
        src/LiveFormatController.cpp
        # Find adapter (internal)
        src/Detail/LiveFindAdapter.h
        src/Detail/LiveFindAdapter.cpp
        # E2.5 Phase J: native QMenu / Widget-bridge context menu
        include/markoff/live/LiveContextMenuHandler.h
        src/LiveContextMenuHandler.cpp
        # E3a: link hit-test + activation-builder helpers
        src/LiveListModelBinding_links.h
        src/LiveListModelBinding_links.cpp
        # 2026-05-20: QQuickWidget wrapper for QWidget hosts (Corbomite-driven).
        include/markoff/live/EditorWidget.h
        src/EditorWidget.cpp
    QML_FILES
        qml/EditorContent.qml
        qml/LiveView.qml
        qml/delegates/CodeBlockDelegate.qml
        qml/delegates/BlockOnlyDelegateBase.qml
        qml/delegates/HorizontalRuleDelegate.qml
        qml/delegates/ImageDelegate.qml
        qml/delegates/MathDelegate.qml
        qml/delegates/RemoteCursorOverlay.qml
        qml/delegates/UnifiedInlineTextDelegate.qml
    RESOURCES
        # KeyDispatch.js — JS-only helper module used by the text-bearing
        # delegates. Placed in RESOURCES (not QML_FILES) because QML_FILES
        # would register it as a 1.0 QML type and the resulting nested
        # qmldir auto-generation breaks module URI resolution (the
        # `import "KeyDispatch.js" as KeyDispatch` callers reach it
        # relatively via the resource path).
        qml/delegates/KeyDispatch.js
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
    Qt6::QuickWidgets
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
