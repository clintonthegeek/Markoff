cmake_minimum_required(VERSION 3.19)
project(Markoff VERSION 0.1.0 LANGUAGES CXX)

# Markoff-family libraries:
#   - libs/rapidyaml       (ryml::ryml — YAML parser, vendored from biojppm/rapidyaml)
#   - libs/markoff-parser  (MarkoffParser::MarkoffParser — tree-sitter Markdown AST + YAML frontmatter)
#   - libs/markoff-core    (Markoff::Core — shared primitives)
#   - libs/markoff-source  (Markoff::Source — Qutepart-based source-mode editor)
#   - libs/markoff-live    (Markoff::Live — live-preview editor widget)
#   - libs/jkqtmathtext    (jkqtmathtext — LaTeX math rendering, used by markoff-reading)
#   - libs/markoff-reading (Markoff::Reading — Obsidian-compatible reading-mode widget)
#
# Can be built standalone OR consumed as a git submodule from a parent project
# (e.g., Corbomite). Qt6 / KF6 / optional helper libs (jkqtmathtext, mmdr) are
# discovered via find_package — parent projects that already find them will
# short-circuit our find_package calls.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

if(NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Collect every executable (tests, test app, scene-demo) in build/bin/
# so paths stay short. Parent projects that consume Markoff as a subdir
# can override by setting CMAKE_RUNTIME_OUTPUT_DIRECTORY before us.
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# rapidyaml uses exceptions in its default callback — required for our
# error-propagation contract in MarkoffParser.
set(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS ON CACHE BOOL "" FORCE)

add_subdirectory(libs/rapidyaml)
add_subdirectory(libs/markoff-parser)

# collabtext — CRDT engine, sibling library symlinked from /home/clinton/dev/collabtext.
# Defensive guard against double-add by parent projects.
if(NOT TARGET collabtext)
    add_subdirectory(libs/collabtext)
endif()

# markoff-foundation — new foundation library replacing markoff-core's role.
# Lives alongside the existing markoff-core, which remains master-maintained.
add_subdirectory(libs/markoff-foundation)

# POC QML view library — Phase 1 source-mode editor; Phase 2 will add live-preview.
add_subdirectory(libs/markoff-view-qml)
add_subdirectory(libs/markoff-source-widget)

add_subdirectory(libs/markoff-core)
add_subdirectory(libs/markoff-source)
add_subdirectory(libs/markoff-live)
# jkqtmathtext is pulled in defensively by libs/markoff-live and
# libs/markoff-reading via `if(NOT TARGET jkqtmathtext)` — don't add it
# again here or CMake sees duplicate add_library calls.
add_subdirectory(libs/markoff-reading)

# Markoff's own integration tests. Parent projects may skip these by setting
# MARKOFF_BUILD_TESTS=OFF before add_subdirectory().
option(MARKOFF_BUILD_TESTS "Build Markoff integration tests" ON)
if(MARKOFF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/markoff)
endif()
