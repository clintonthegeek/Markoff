cmake_minimum_required(VERSION 3.19)
project(markoff-parser VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
# yaml-cpp replaced by ryml (added at top-level CMakeLists.txt)

pkg_check_modules(TREESITTER REQUIRED IMPORTED_TARGET tree-sitter)

# Tree-sitter markdown grammar (vendored)
set(TS_MD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/tree-sitter-markdown)
add_library(ts-markdown-parser STATIC
    ${TS_MD_DIR}/tree-sitter-markdown/src/parser.c
    ${TS_MD_DIR}/tree-sitter-markdown/src/scanner.c
    ${TS_MD_DIR}/tree-sitter-markdown-inline/src/parser.c
    ${TS_MD_DIR}/tree-sitter-markdown-inline/src/scanner.c
)
target_include_directories(ts-markdown-parser PRIVATE
    ${TS_MD_DIR}/tree-sitter-markdown/src
    ${TS_MD_DIR}/tree-sitter-markdown-inline/src
)
target_compile_definitions(ts-markdown-parser PRIVATE
    EXTENSION_WIKI_LINK
    EXTENSION_TAGS
)
set_target_properties(ts-markdown-parser PROPERTIES POSITION_INDEPENDENT_CODE ON)

# MarkoffParser library
add_library(markoff-parser STATIC
    src/TreeSitterParser.cpp
    src/SourceSpan.cpp
    src/MarkdownSplitter.cpp
    src/TableHandler.cpp
    src/Document.cpp
    src/LinkTextParser.cpp
    src/YamlValue.cpp
)
set_target_properties(markoff-parser PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(MarkoffParser::MarkoffParser ALIAS markoff-parser)

target_include_directories(markoff-parser
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${TS_MD_DIR}/tree-sitter-markdown/bindings/c
            ${TS_MD_DIR}/tree-sitter-markdown-inline/bindings/c
)

target_link_libraries(markoff-parser
    PUBLIC
        Qt6::Core
    PRIVATE
        Qt6::Gui
        Qt6::Widgets
        PkgConfig::TREESITTER
        ts-markdown-parser
        ryml::ryml
)

enable_testing()
add_subdirectory(tests)
