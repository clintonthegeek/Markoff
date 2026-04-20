cmake_minimum_required(VERSION 3.19)
project(markoff_reading VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg SvgWidgets)
find_package(KF6SyntaxHighlighting REQUIRED)

# jkqtmathtext: pulled in as a sibling add_subdirectory by the parent tree.
# Defensively add it if we're being configured standalone.
if(NOT TARGET jkqtmathtext)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../jkqtmathtext
                     ${CMAKE_CURRENT_BINARY_DIR}/jkqtmathtext)
endif()

# markoff-reading library — Obsidian-compatible Reading-mode widget.
#
# Phase A of the tri-view absorption: Corbomite::Core, Corbomite::Storage,
# and the mmdr Rust crate are replaced by compile-only shim headers under
# `stubs/`. Runtime features that depend on those deps (embeds, mermaid
# rendering) are broken until Phase B absorbs the real sources.
#
# Phase B bridge: a consuming project (CorbomiteApp) can set
# MARKOFF_READING_USE_REAL_COREDEPS=ON before adding this subdirectory to
# drop the stubs and link the real Corbomite::Core + mmdr targets it
# provides. Default OFF keeps standalone Markoff builds on the stubs.
# See docs/specs/2026-04-20-phase-b-corbomite-migration.md for the
# full migration shape; the DI seam that retires this option lands in
# Phase C.
option(MARKOFF_READING_USE_REAL_COREDEPS
    "Drop the Corbomite::Core / mmdr stub headers and link the real \
implementations provided by a consuming project (e.g. CorbomiteApp). \
OFF for standalone Markoff builds; ON when Markoff is a submodule inside \
a host that supplies these types."
    OFF)
add_library(markoff_reading STATIC
    include/markoff/reading/CodeBlockHighlighter.h
    src/CodeBlockHighlighter.cpp
    include/markoff/reading/ReadingView.h
    src/ReadingView.cpp
    include/markoff/reading/ReadingSection.h
    src/ReadingSection.cpp
    include/markoff/reading/ReadingPipeline.h
    src/ReadingPipeline.cpp
    include/markoff/reading/ReadingParseWorker.h
    src/ReadingParseWorker.cpp
    include/markoff/reading/ReadingViewConstants.h
    include/markoff/reading/SectionLayout.h
    src/SectionLayout.cpp
    include/markoff/reading/SectionRecyclePool.h
    src/SectionRecyclePool.cpp
    include/markoff/reading/VirtualScrollController.h
    src/VirtualScrollController.cpp
    include/markoff/reading/VaultResourceProvider.h
    include/markoff/reading/LinkRenderer.h
    src/LinkRenderer.cpp
    include/markoff/reading/EmbedRenderer.h
    src/EmbedRenderer.cpp
    src/SpanRenderer.h
    src/SpanRenderer.cpp
    src/ReadingSearchAdapter.h
    src/ReadingSearchAdapter.cpp
    src/ReadingMathObject.h
    src/ReadingMathObject.cpp
    src/MermaidRenderer.h
    src/MermaidRenderer.cpp
    include/markoff/reading/styling/ParagraphStyle.h
    src/styling/ParagraphStyle.cpp
    include/markoff/reading/styling/CharacterStyle.h
    src/styling/CharacterStyle.cpp
    include/markoff/reading/styling/StyleManager.h
    src/styling/StyleManager.cpp
)
set_target_properties(markoff_reading PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Markoff::Reading ALIAS markoff_reading)

target_include_directories(markoff_reading
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MARKOFF_READING_USE_REAL_COREDEPS)
    # Real-deps mode (CorbomiteApp): host must provide Corbomite::Core and
    # an mmdr target. Corbomite::Storage is intentionally omitted — Phase A
    # survey found zero direct includes from readingview sources; Phase B
    # Task 5 validates this empirically and drops the stub if so.
    target_link_libraries(markoff_reading
        PRIVATE
            Corbomite::Core
            mmdr
    )
else()
    # Standalone mode: compile against the Phase-A stub shims. Exposed
    # PUBLIC (build-tree only) so in-tree tests that subclass
    # `Corbomite::Core::VaultResourceProvider` or mention
    # `Corbomite::Core::EmbedRegistry` can still compile. `mmdr_ffi.h`
    # lives at the stubs dir root so MermaidRenderer.cpp's
    # `#include "mmdr_ffi.h"` picks up the shim.
    target_include_directories(markoff_reading PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stubs>
    )
endif()

target_link_libraries(markoff_reading
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        Qt6::SvgWidgets
        KF6::SyntaxHighlighting
        markoff_core
    PRIVATE
        MarkoffParser::MarkoffParser
        jkqtmathtext
)

enable_testing()
add_subdirectory(tests)
