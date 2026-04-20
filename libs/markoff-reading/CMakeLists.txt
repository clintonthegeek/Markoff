cmake_minimum_required(VERSION 3.19)
project(readingview VERSION 0.1.0 LANGUAGES CXX)

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

# ReadingView library (Obsidian-compatible Reading-mode widget).
# Peer to libs/markoff — DOES NOT depend on Markoff.
add_library(readingview STATIC
    include/corbomite/readingview/CodeBlockHighlighter.h
    src/CodeBlockHighlighter.cpp
    include/corbomite/readingview/ReadingView.h
    src/ReadingView.cpp
    include/corbomite/readingview/ReadingSection.h
    src/ReadingSection.cpp
    include/corbomite/readingview/ReadingPipeline.h
    src/ReadingPipeline.cpp
    include/corbomite/readingview/ReadingParseWorker.h
    src/ReadingParseWorker.cpp
    include/corbomite/readingview/ReadingViewConstants.h
    include/corbomite/readingview/SectionLayout.h
    src/SectionLayout.cpp
    include/corbomite/readingview/SectionRecyclePool.h
    src/SectionRecyclePool.cpp
    include/corbomite/readingview/VirtualScrollController.h
    src/VirtualScrollController.cpp
    include/corbomite/readingview/VaultResourceProvider.h
    include/corbomite/readingview/LinkRenderer.h
    src/LinkRenderer.cpp
    include/corbomite/readingview/EmbedRenderer.h
    src/EmbedRenderer.cpp
    src/SpanRenderer.h
    src/SpanRenderer.cpp
    src/ReadingMathObject.h
    src/ReadingMathObject.cpp
    src/MermaidRenderer.h
    src/MermaidRenderer.cpp
    include/corbomite/readingview/styling/ParagraphStyle.h
    src/styling/ParagraphStyle.cpp
    include/corbomite/readingview/styling/CharacterStyle.h
    src/styling/CharacterStyle.cpp
    include/corbomite/readingview/styling/StyleManager.h
    src/styling/StyleManager.cpp
)
set_target_properties(readingview PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Corbomite::ReadingView ALIAS readingview)

target_include_directories(readingview
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(readingview
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        Qt6::SvgWidgets
        KF6::SyntaxHighlighting
        Corbomite::Core
        Corbomite::Storage
    PRIVATE
        MarkoffParser::MarkoffParser
        jkqtmathtext
        mmdr
)

enable_testing()
add_subdirectory(tests)
