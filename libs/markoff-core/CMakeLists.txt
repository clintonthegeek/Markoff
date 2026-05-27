cmake_minimum_required(VERSION 3.19)
project(markoff_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

# Library target: STATIC so it can be embedded in test apps + future views.
# Sources are added incrementally as tasks land. Initial skeleton is just
# the export header so the target builds.
add_library(markoff_core STATIC
    include/markoff/core/MarkoffCoreExport.h
    include/markoff/core/Origin.h
    include/markoff/core/AnchorJson.h
    include/markoff/core/Selection.h
    include/markoff/core/FoldRef.h
    include/markoff/core/MarkoffDocument.h
    # Restored 2026-05-20 driven by Corbomite port pull on EmbedRegistry.
    include/markoff/core/MarkdownRenderChild.h
    include/markoff/core/EmbedDepthGuard.h
    include/markoff/core/EmbedRegistry.h
    include/markoff/core/vault/ResourceProvider.h
    src/MarkdownRenderChild.cpp
    include/markoff/core/SessionParams.h
    include/markoff/core/Session.h
    include/markoff/core/Theme.h
    include/markoff/core/CodeTokenKind.h
    include/markoff/core/CodeSpan.h
    include/markoff/core/LinkKind.h
    include/markoff/core/LinkActivation.h
    include/markoff/core/LinkService.h
    include/markoff/core/DefaultLinkService.h
    include/markoff/core/SyntaxHighlightService.h
    include/markoff/core/Kf6SyntaxHighlightService.h
    include/markoff/core/RenderedBlock.h
    include/markoff/core/CodeBlockProcessor.h
    include/markoff/core/CodeBlockProcessorRegistry.h
    # --- Completion/ (public) ---
    include/markoff/core/CompletionTrigger.h
    include/markoff/core/CompletionContext.h
    include/markoff/core/CompletionCandidate.h
    include/markoff/core/CompletionDetector.h
    include/markoff/core/CompletionProvider.h
    include/markoff/core/CompletionRegistry.h
    include/markoff/core/EmojiCompletionProvider.h
    include/markoff/core/SourceTextDocumentBinding.h
    include/markoff/core/SearchEngine.h
    include/markoff/core/FindController.h
    include/markoff/core/MarkoffServices.h
    # --- Cmd/ (public) ---
    # Cmd aggregate
    include/markoff/core/Cmd.h
    include/markoff/core/Cmd/Edit.h
    include/markoff/core/Cmd/D2.h
    src/MarkoffDocumentPrivate.h
    src/SessionPrivate.h
    src/AnchorConversion.cpp
    src/BlockAnchorComputation.cpp
    src/AnchorJson.cpp
    src/Selection.cpp
    src/FoldRef.cpp
    src/MarkoffDocument.cpp
    src/Session.cpp
    src/Theme.cpp
    src/LinkService.cpp
    src/DefaultLinkService.cpp
    src/SyntaxHighlightService.cpp
    src/Kf6SyntaxHighlightService.cpp
    src/SourceTextDocumentBinding.cpp
    src/SearchEngine.cpp
    src/FindController.cpp
    src/CodeBlockProcessorRegistry.cpp
    src/CompletionDetector.cpp
    src/CompletionProvider.cpp
    src/CompletionRegistry.cpp
    src/EmojiCompletionProvider.cpp
    src/EmojiData.h
    include/markoff/core/UndoLog.h
    src/UndoLog.cpp
    include/markoff/core/CrdtProxies.h
    src/CrdtProxies.cpp
    # --- Sibling map public headers (Phase 5) ---
    include/markoff/core/KindTagMap.h
    include/markoff/core/BlockAttrsMap.h
    include/markoff/core/FrontmatterMap.h
    include/markoff/core/LinkRefMap.h
    include/markoff/core/FootnoteDefMap.h
    # --- Save (Phase 8) ---
    include/markoff/core/BlockSerializer.h
    src/BlockSerializers.cpp
    # --- GC / WatermarkCoordinator (Phase 9) ---
    include/markoff/core/WatermarkCoordinator.h
    src/WatermarkCoordinator.cpp
    # --- InlineParseCache (Phase 10) ---
    include/markoff/core/InlineParseCache.h
    src/InlineParseCache.cpp
    # --- Cmd/ (impl) ---
    src/Cmd/Edit.cpp
    src/Cmd/D2.cpp
    # --- Detail/ (shared internal helpers) ---
    include/markoff/core/Detail/FlatBlockResolve.h
    src/Detail/FlatBlockResolve.cpp
    # --- MarkdownView polymorphic base (Task 4.5) ---
    include/markoff/core/MarkdownView.h
    src/MarkdownView.cpp
    # --- Sibling-map op wire envelope (D5 §3.1) ---
    include/markoff/core/SiblingMapOpHeader.h
    src/SiblingMapOpHeader.cpp
    # --- Bundle encode/decode (D5 §3.2) ---
    include/markoff/core/MarkoffSerializer.h
    src/MarkoffSerializer.cpp
    # --- Collab boundary types (D5) — must be in SOURCES so AUTOMOC compiles
    #     moc_ActionId.cpp (defines Markoff::staticMetaObject from Q_NAMESPACE).
    include/markoff/core/ActionId.h
    include/markoff/core/MarkoffOp.h
    # --- Cursor types (D5 phase 7) — moved from markoff-live to markoff-core
    #     so MarkoffDocument can hold/emit Cursor values.
    include/markoff/core/Cursor.h
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
        collabtext
        Markoff::Parser
)

# Alias under the Markoff:: namespace for consumers.
add_library(Markoff::Core ALIAS markoff_core)

# Tests.
if(NOT DEFINED MARKOFF_CORE_BUILD_TESTS)
    set(MARKOFF_CORE_BUILD_TESTS ON)
endif()
if(MARKOFF_CORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
