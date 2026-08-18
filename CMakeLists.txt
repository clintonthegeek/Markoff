cmake_minimum_required(VERSION 3.19)
# Version tracks the v0.7.x tag series (see git tags v0.7.0-*).
# Markoff::Live::version() derives from PROJECT_VERSION — keep in sync.
project(Markoff VERSION 0.7.0 LANGUAGES CXX)

# Markoff-family libraries:
#   - libs/rapidyaml          (ryml::ryml — YAML parser, vendored)
#   - libs/markoff-parser     (Markoff::Parser — tree-sitter Markdown AST + YAML frontmatter)
#   - libs/collabtext         (CRDT text engine, git submodule)
#   - libs/markoff-core       (Markoff::Core — MarkoffDocument + Sessions + flat-view binding)
#   - libs/markoff-live       (QML live-preview view leaf + Markoff::Live::EditorWidget)
#   - libs/markoff-source     (QPlainTextEdit-based source widget)
#   - libs/markoff-styled     (QTextEdit-based inline-styled editor)
#   - libs/markoff-canvas     (Markoff::Canvas — projection view spike, QTextLayout per block)
#   - libs/jkqtmathtext       (LaTeX math rendering, vendored)
#
# Can be built standalone OR consumed as a git submodule from a parent project.
# Qt6 / KF6 are discovered via find_package — parent projects that already find
# them will short-circuit our find_package calls.

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
# error-propagation contract in Markoff::Parser.
set(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS ON CACHE BOOL "" FORCE)

add_subdirectory(libs/rapidyaml)

# rapidyaml + bundled c4core are non-Qt third-party C++ libraries; they
# inherit the project-scope CMAKE_AUTOMOC / CMAKE_AUTOUIC ON flags above
# and emit dev warnings ("No valid Qt version found for target"). Opt
# them out. Guarded with if(TARGET ...) so parent projects providing
# ryml via find_package don't see clobbered properties. See
# docs/2026-05-03-cmake-warnings-rationale.md.
if(TARGET ryml)
    set_target_properties(ryml PROPERTIES AUTOMOC OFF AUTOUIC OFF)
endif()
if(TARGET c4core)
    set_target_properties(c4core PROPERTIES AUTOMOC OFF AUTOUIC OFF)
endif()

add_subdirectory(libs/markoff-parser)

# collabtext — CRDT engine, vendored as a git submodule (git@codeberg.org:clintonthegeek/collabtext.git).
# Defensive guard against double-add by parent projects.
if(NOT TARGET collabtext)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/collabtext/CMakeLists.txt")
        message(FATAL_ERROR
            "libs/collabtext is empty — the collabtext submodule has not been initialized.\n"
            "Run: git submodule update --init --recursive\n"
            "(Requires SSH access to Codeberg; see ~/dev/CLAUDE.md.)")
    endif()
    add_subdirectory(libs/collabtext)
endif()

add_subdirectory(libs/markoff-core)
# jkqtmathtext — LaTeX math rendering for Math blocks (canvas / styled / live).
if(NOT TARGET jkqtmathtext)
    add_subdirectory(libs/jkqtmathtext)
endif()
# MARKOFF_BUILD_LIVE — QML LivePreview leaf. Corbomite sets this OFF
# (Cluster K Phase 5: canvas is the sole LivePreview engine). Standalone
# Markoff builds keep the default ON so the leaf remains available there.
option(MARKOFF_BUILD_LIVE "Build markoff-live (QML LivePreview leaf)" ON)
if(MARKOFF_BUILD_LIVE)
    add_subdirectory(libs/markoff-live)
endif()
add_subdirectory(libs/markoff-source)
add_subdirectory(libs/markoff-styled)
add_subdirectory(libs/markoff-canvas)

# Apps (markoff-collab-testapp, future scene-demo) are demo binaries that link
# Qt::QuickWidgets and other extra components. Default-ON for standalone builds,
# default-OFF when consumed as a subdirectory so parent projects don't have to
# pull QuickWidgets just to satisfy the demo app's link line.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_MARKOFF_BUILD_APPS_DEFAULT ON)
else()
    set(_MARKOFF_BUILD_APPS_DEFAULT OFF)
endif()
option(MARKOFF_BUILD_APPS "Build Markoff demo apps (markoff-collab-testapp, etc.)" ${_MARKOFF_BUILD_APPS_DEFAULT})
# Demo apps currently link markoff_live; skip them when LIVE is OFF.
if(MARKOFF_BUILD_APPS AND MARKOFF_BUILD_LIVE)
    add_subdirectory(apps)
elseif(MARKOFF_BUILD_APPS AND NOT MARKOFF_BUILD_LIVE)
    message(STATUS "MARKOFF_BUILD_APPS ignored because MARKOFF_BUILD_LIVE=OFF")
endif()
unset(_MARKOFF_BUILD_APPS_DEFAULT)

option(MARKOFF_BUILD_TESTS "Build Markoff integration tests" ON)
if(MARKOFF_BUILD_TESTS)
    enable_testing()
endif()
