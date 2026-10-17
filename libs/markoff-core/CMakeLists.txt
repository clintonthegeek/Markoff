cmake_minimum_required(VERSION 3.19)
project(markoff_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

# SHARED (Cluster P Phase P6, Corbomite-side plan): a STATIC markoff_core
# gets a private copy of Qt metaobjects/RTTI in every consumer, so a host
# and a dlopen()'d plugin that both touch a markoff_core type end up with
# two distinct QMetaObjects and qobject_cast silently fails across that
# boundary. No -fvisibility=hidden is set anywhere in this tree, so the
# existing no-op MARKOFF_CORE_EXPORT macro needs no changes.
add_library(markoff_core SHARED
    include/markoff/core/MarkoffCoreExport.h
    include/markoff/core/Origin.h
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
    src/SessionPrivate.h
    src/Selection.cpp
    src/FoldRef.cpp
    src/Session.cpp
    src/Theme.cpp
    src/LinkService.cpp
    src/DefaultLinkService.cpp
    src/SyntaxHighlightService.cpp
    src/Kf6SyntaxHighlightService.cpp
    include/markoff/core/StructuralKeyHandler.h
    include/markoff/core/KindInference.h
    include/markoff/core/TextUnits.h
    src/KindInference.cpp
    src/SearchEngine.cpp
    src/FindController.cpp
    src/CodeBlockProcessorRegistry.cpp
    src/CompletionProvider.cpp
    src/CompletionRegistry.cpp
    src/EmojiCompletionProvider.cpp
    src/EmojiData.h
    # --- Sibling map public headers (Phase 5) ---
    include/markoff/core/KindTagMap.h
    include/markoff/core/BlockAttrsMap.h
    include/markoff/core/FrontmatterMap.h
    include/markoff/core/LinkRefMap.h
    include/markoff/core/FootnoteDefMap.h
    # --- Save (Phase 8) ---
    include/markoff/core/BlockSerializer.h
    src/BlockSerializers.cpp
    # --- InlineParseCache (Phase 10) ---
    include/markoff/core/InlineParseCache.h
    src/InlineParseCache.cpp
    # --- Cmd/ (impl) ---
    src/Cmd/Edit.cpp
    # --- Detail/ (shared internal helpers) ---
    include/markoff/core/Detail/FlatBlockResolve.h
    src/Detail/FlatBlockResolve.cpp
    # --- FormatOps (MarkdownView contract v2 §5) — widget-free format
    #     verbs hoisted from markoff-source's Editor ---
    include/markoff/core/FormatOps.h
    include/markoff/core/ClipboardCodec.h
    src/ClipboardCodec.cpp
    # --- MarkdownView polymorphic base (Task 4.5) ---
    include/markoff/core/MarkdownView.h
    src/MarkdownView.cpp
    # ActionId is a Q_NAMESPACE. Listing the header compiles moc_ActionId.cpp.
    include/markoff/core/ActionId.h
)

set_target_properties(markoff_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(markoff_core PRIVATE src/MarkoffDocumentVNext.cpp)
target_sources(markoff_core PRIVATE src/FormatOpsVNext.cpp)
target_sources(markoff_core PRIVATE src/Cmd/D2VNext.cpp)
target_sources(markoff_core PRIVATE src/SourceTextDocumentBindingVNext.cpp)
target_sources(markoff_core PRIVATE src/StructuralKeyHandlerVNext.cpp)

target_include_directories(markoff_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(markoff_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        KF6::SyntaxHighlighting
        Markoff::Parser
        Markoff::TextUnits
)
target_link_libraries(markoff_core PUBLIC Markoff::CollabTextVNext)

# Alias under the Markoff:: namespace for consumers.
add_library(Markoff::Core ALIAS markoff_core)

# Install rules — only meaningful when built as a Corbomite subdirectory
# (KDEInstallDirs is included by the parent tree before add_subdirectory).
# A standalone Markoff build skips this, same as before this library was
# SHARED (it had no install rule at all).
if(DEFINED KDE_INSTALL_LIBDIR)
    if(APPLE)
        set(_markoff_core_install_rpath "@loader_path")
    elseif(UNIX)
        set(_markoff_core_install_rpath "$ORIGIN")
    endif()
    if(_markoff_core_install_rpath)
        set_target_properties(markoff_core PROPERTIES
            INSTALL_RPATH "${_markoff_core_install_rpath}"
        )
    endif()
    # EXPORT CorbomiteTargets: Corbomite's own corbomite-vault privately
    # links Markoff::Parser, and CMake's install(EXPORT) validation
    # requires every target an exported SHARED library links — public or
    # private — to itself be in an export set (or IMPORTED). Since Markoff
    # is only ever installed as part of Corbomite (this block is inert
    # standalone, gated on KDE_INSTALL_LIBDIR existing at all), joining
    # Corbomite's own export set is the correct scope, not a layering
    # violation — there is no separate MarkoffTargets consumer to protect.
    install(TARGETS markoff_core
        EXPORT CorbomiteTargets
        RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    )
    install(DIRECTORY include/markoff
        DESTINATION ${KDE_INSTALL_INCLUDEDIR}
    )
endif()

# Tests.
if(NOT DEFINED MARKOFF_CORE_BUILD_TESTS)
    set(MARKOFF_CORE_BUILD_TESTS ON)
endif()
if(MARKOFF_CORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
