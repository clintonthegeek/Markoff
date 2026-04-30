cmake_minimum_required(VERSION 3.19)
project(markoff_foundation VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

# Library target: STATIC so it can be embedded in test apps + future views.
# Sources are added incrementally as tasks land. Initial skeleton is just
# the export header so the target builds.
add_library(markoff_foundation STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    include/markoff-foundation/Origin.h
    include/markoff-foundation/MarkoffEdit.h
    include/markoff-foundation/AnchorJson.h
    include/markoff-foundation/Selection.h
    include/markoff-foundation/FoldRef.h
    include/markoff-foundation/MarkoffDocument.h
    include/markoff-foundation/RenderPhases.h
    include/markoff-foundation/SessionParams.h
    include/markoff-foundation/Session.h
    include/markoff-foundation/Theme.h
    include/markoff-foundation/CodeTokenKind.h
    include/markoff-foundation/CodeSpan.h
    include/markoff-foundation/LinkKind.h
    include/markoff-foundation/LinkActivation.h
    include/markoff-foundation/LinkService.h
    include/markoff-foundation/DefaultLinkService.h
    include/markoff-foundation/SyntaxHighlightService.h
    include/markoff-foundation/Kf6SyntaxHighlightService.h
    include/markoff-foundation/RenderedBlock.h
    include/markoff-foundation/CodeBlockProcessor.h
    include/markoff-foundation/CodeBlockProcessorRegistry.h
    # --- Completion/ (public) ---
    include/markoff-foundation/CompletionTrigger.h
    include/markoff-foundation/CompletionContext.h
    include/markoff-foundation/CompletionCandidate.h
    include/markoff-foundation/CompletionDetector.h
    include/markoff-foundation/CompletionProvider.h
    include/markoff-foundation/CompletionRegistry.h
    include/markoff-foundation/EmojiCompletionProvider.h
    include/markoff-foundation/CommandFacade.h
    include/markoff-foundation/SourceTextDocumentBinding.h
    include/markoff-foundation/SearchEngine.h
    include/markoff-foundation/ReplaceController.h
    include/markoff-foundation/MarkoffServices.h
    # --- Cmd/ (public) ---
    # Cmd aggregate
    include/markoff-foundation/Cmd.h
    include/markoff-foundation/Cmd/Edit.h
    include/markoff-foundation/Cmd/InlineFormat.h
    include/markoff-foundation/Cmd/Block.h
    include/markoff-foundation/Cmd/Insert.h
    src/MarkoffDocumentPrivate.h
    src/SessionPrivate.h
    src/ParsePool.h
    src/ParsePoolWorker.h
    src/IncrementalParseSession.h
    src/MarkoffEdit.cpp
    src/AnchorJson.cpp
    src/Selection.cpp
    src/FoldRef.cpp
    src/MarkoffDocument.cpp
    src/Session.cpp
    src/ParsePool.cpp
    src/ParsePoolWorker.cpp
    src/IncrementalParseSession.cpp
    src/Theme.cpp
    src/LinkService.cpp
    src/DefaultLinkService.cpp
    src/SyntaxHighlightService.cpp
    src/Kf6SyntaxHighlightService.cpp
    src/CommandFacade.cpp
    src/SourceTextDocumentBinding.cpp
    src/SearchEngine.cpp
    src/ReplaceController.cpp
    src/CodeBlockProcessorRegistry.cpp
    src/CompletionDetector.cpp
    src/CompletionProvider.cpp
    src/CompletionRegistry.cpp
    src/EmojiCompletionProvider.cpp
    src/EmojiData.h
    # --- Cmd/ (impl) ---
    src/Cmd/Edit.cpp
    src/Cmd/Helpers.h
    src/Cmd/Helpers.cpp
    src/Cmd/InlineFormat.cpp
    src/Cmd/Block.cpp
    src/Cmd/Insert.cpp
)

target_include_directories(markoff_foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(markoff_foundation
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        KF6::SyntaxHighlighting
        collabtext
        MarkoffParser::MarkoffParser
)

# Alias under the Markoff:: namespace for consumers.
add_library(Markoff::Foundation ALIAS markoff_foundation)

# Tests.
if(NOT DEFINED MARKOFF_FOUNDATION_BUILD_TESTS)
    set(MARKOFF_FOUNDATION_BUILD_TESTS ON)
endif()
if(MARKOFF_FOUNDATION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
