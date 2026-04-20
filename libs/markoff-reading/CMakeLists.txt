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

# Phase-A stubs live under `stubs/`. Because they use the original
# `corbomite/core/...` / `corbomite/storage/...` include paths, adding the
# stubs dir to the PRIVATE include path is enough — no source edits needed
# to re-route includes. `mmdr_ffi.h` lives at the stubs dir root so
# MermaidRenderer.cpp's `#include "mmdr_ffi.h"` picks up the shim.
target_include_directories(markoff_reading
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Phase-A stub shims. Exposed PUBLIC (build-tree only) so in-tree
        # tests that subclass `Corbomite::Core::VaultResourceProvider` or
        # mention `Corbomite::Core::EmbedRegistry` can still compile.
        # Remove when the real Corbomite::Core / Corbomite::Storage
        # sources land in Phase B.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stubs>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

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
