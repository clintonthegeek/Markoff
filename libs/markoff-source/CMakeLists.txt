# SPDX-License-Identifier: GPL-3.0-or-later
#
# Corbomite-style CMakeLists.txt for the vendored, shaped-in-place qutepart-cpp
# fork. See PROVENANCE.md for upstream details and the 8-phase plan under
# ../../docs/superpowers/plans/2026-04-15-qutepart-corbomite-fork.md.

cmake_minimum_required(VERSION 3.19)
project(qutepart-corbomite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets)

# Inherited sources (mirrors upstream's CMake SRC list, 1:1).
# Kept verbatim for Phase 1. Trimming happens in Phases 4 (hl/), 5 (indent/),
# 6 (theme.cpp + theme.qrc).
set(qutepart_corbomite_SRCS
    # Resources (kept for Phase 1; deleted in Phases 4 & 6).
    qutepart-syntax-files.qrc
    qutepart-theme-data.qrc

    # Top-level editor sources.
    src/qutepart.cpp
    src/lines.cpp
    src/char_iterator.cpp
    src/text_block_utils.cpp
    src/text_block_flags.cpp
    src/bracket_highlighter.cpp
    src/side_areas.cpp
    src/completer.cpp
    src/theme.cpp
    src/html_delegate.cpp
    src/hl_factory.cpp

    # Bundled Kate-XML highlighting engine (removed in Phase 4).
    src/hl/context.cpp
    src/hl/language.cpp
    src/hl/loader.cpp
    src/hl/rules.cpp
    src/hl/syntax_highlighter.cpp
    src/hl/style.cpp
    src/hl/context_stack.cpp
    src/hl/context_switcher.cpp
    src/hl/text_block_user_data.cpp
    src/hl/text_to_match.cpp
    src/hl/match_result.cpp
    src/hl/language_db_generated.cpp
    src/hl/language_db.cpp
    src/hl/text_type.cpp

    # Indent algorithms (trimmed to markdown-only in Phase 5).
    src/indent/indenter.cpp
    src/indent/indent_funcs.cpp
    src/indent/alg_impl.cpp
    src/indent/alg_lisp.cpp
    src/indent/alg_xml.cpp
    src/indent/alg_scheme.cpp
    src/indent/alg_python.cpp
    src/indent/alg_ruby.cpp
    src/indent/alg_cstyle.cpp
)

# Headers listed so AUTOMOC sees Q_OBJECT declarations and the generator
# picks them up even when not referenced by a translation unit directly.
set(qutepart_corbomite_HDRS
    include/qutepart/qutepart.h
    include/qutepart/theme.h

    src/bracket_highlighter.h
    src/char_iterator.h
    src/completer.h
    src/context_stack.h
    src/hl_factory.h
    src/html_delegate.h
    src/side_areas.h
    src/text_block_flags.h
    src/text_block_user_data.h
    src/text_block_utils.h
    src/text_pos.h

    src/hl/context.h
    src/hl/context_switcher.h
    src/hl/language.h
    src/hl/loader.h
    src/hl/match_result.h
    src/hl/rules.h
    src/hl/style.h
    src/hl/syntax_highlighter.h
    src/hl/text_to_match.h
    src/hl/text_type.h

    src/indent/alg_cstyle.h
    src/indent/alg_impl.h
    src/indent/alg_lisp.h
    src/indent/alg_python.h
    src/indent/alg_ruby.h
    src/indent/alg_scheme.h
    src/indent/alg_xml.h
    src/indent/indent_funcs.h
    src/indent/indenter.h
)

add_library(qutepart-corbomite STATIC
    ${qutepart_corbomite_SRCS}
    ${qutepart_corbomite_HDRS}
)
set_target_properties(qutepart-corbomite PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Corbomite::QutepartSource ALIAS qutepart-corbomite)

target_include_directories(qutepart-corbomite
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/qutepart>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(qutepart-corbomite
    PUBLIC
        Qt6::Core
        Qt6::Widgets
)

# No install rules: this is an internal static library consumed via
# add_subdirectory from the Corbomite top-level build.

enable_testing()
add_subdirectory(tests)
