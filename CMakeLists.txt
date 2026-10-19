cmake_minimum_required(VERSION 3.19)
# Version tracks the v0.7.x tag series (see git tags v0.7.0-*).
project(Markoff VERSION 0.7.0 LANGUAGES CXX)

# Markoff-family libraries:
#   - libs/rapidyaml          (ryml::ryml — YAML parser, vendored)
#   - libs/markoff-parser     (Markoff::Parser — tree-sitter Markdown AST + YAML frontmatter)
#   - libs/markoff-core       (Markoff::Core — MarkoffDocument + Sessions + flat-view binding)
#   - libs/markoff-canvas     (primary block-projection editor)
#   - libs/markoff-source     (QPlainTextEdit-based source widget)
#   - libs/markoff-styled     (QTextEdit-based inline-styled editor)
#   - libs/jkqtmathtext       (LaTeX math rendering, vendored)
#
# Can be built standalone OR consumed as a git submodule from a parent project.
# Qt6 / KF6 are discovered via find_package — parent projects that already find
# them will short-circuit our find_package calls.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# Source and Core publicly link this imported target. Discover it in the root
# directory so Qt's deferred target finalizers in app/test subdirectories can
# also resolve the dependency while collecting plugin and QML requirements.
find_package(KF6SyntaxHighlighting REQUIRED)

# Cluster P Phase P6 (Corbomite-side plan): markoff_core/canvas/source/styled,
# markoff-parser, ts-markdown-parser and collabtext are all SHARED now, each
# with its own explicit POSITION_INDEPENDENT_CODE. Vendored STATIC deps they
# still link (rapidyaml's ryml/c4core) need PIC too or the final .so link
# fails. Corbomite already sets this globally when Markoff builds as its
# subdirectory (CMakeLists.txt top-level); set it here too so a standalone
# Markoff build (this repo's own primary use case) doesn't need it supplied
# by a parent.
if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

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

# The canonical test switch. ON for a standalone build, OFF when this project is
# consumed as a subdirectory; every leaf follows it unless the consumer
# overrides that leaf's own gate (MARKOFF_<LEAF>_BUILD_TESTS).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_MARKOFF_TESTS_DEFAULT ON)
else()
    set(_MARKOFF_TESTS_DEFAULT OFF)
endif()
option(MARKOFF_BUILD_TESTS "Build the Markoff test suites" ${_MARKOFF_TESTS_DEFAULT})
unset(_MARKOFF_TESTS_DEFAULT)
foreach(_markoff_leaf_tests
        MARKOFF_PARSER_BUILD_TESTS
        MARKOFF_CORE_BUILD_TESTS
        MARKOFF_SOURCE_BUILD_TESTS
        MARKOFF_STYLED_BUILD_TESTS
        MARKOFF_CANVAS_BUILD_TESTS
        MARKOFF_EMBED_BUILD_TESTS)
    if(NOT DEFINED ${_markoff_leaf_tests})
        set(${_markoff_leaf_tests} ${MARKOFF_BUILD_TESTS})
    endif()
endforeach()
unset(_markoff_leaf_tests)

add_subdirectory(libs/markoff-parser)
add_subdirectory(libs/markoff-text-units)

# Markoff has one document engine: the provider-backed CollabText Core
# candidate. Host builds must provide the installed package before adding this
# project; Markoff never falls back to its retired pinned submodule/API.
if(DEFINED MARKOFF_USE_COLLABTEXT_VNEXT AND NOT MARKOFF_USE_COLLABTEXT_VNEXT)
    message(FATAL_ERROR
        "Pinned-engine builds have been removed; Markoff always requires the installed CollabText Core package")
endif()
if(DEFINED MARKOFF_BUILD_LIVE AND MARKOFF_BUILD_LIVE)
    message(FATAL_ERROR
        "The retired markoff-live target has been removed from the supported build")
endif()
set(MARKOFF_USE_COLLABTEXT_VNEXT ON)
if(TARGET collabtext)
    message(FATAL_ERROR
        "Markoff requires the installed CollabText Core package and cannot share a target graph with the retired pinned collabtext target")
endif()
find_package(collabtext CONFIG REQUIRED COMPONENTS core)
add_subdirectory(libs/markoff-collabtext-vnext)

add_subdirectory(libs/markoff-core)
# Optional embed runtime + backends. A host that links neither gets source-text
# fallback for embedded content; no leaf depends on this.
add_subdirectory(libs/markoff-embed)
# jkqtmathtext — LaTeX math rendering for Math blocks (canvas / styled).
if(NOT TARGET jkqtmathtext)
    add_subdirectory(libs/jkqtmathtext)
endif()
add_subdirectory(libs/markoff-source)
add_subdirectory(libs/markoff-styled)
add_subdirectory(libs/markoff-canvas)

# Demo apps are default-ON for standalone builds and default-OFF when consumed
# as a subdirectory. The collaboration test app exercises the provider-backed
# Canvas consumer.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_MARKOFF_BUILD_APPS_DEFAULT ON)
else()
    set(_MARKOFF_BUILD_APPS_DEFAULT OFF)
endif()
option(MARKOFF_BUILD_APPS "Build Markoff demo apps (markoff-collab-testapp, etc.)" ${_MARKOFF_BUILD_APPS_DEFAULT})
# The collaboration test app exercises the provider-backed document path.
if(MARKOFF_BUILD_APPS)
    add_subdirectory(apps)
endif()
unset(_MARKOFF_BUILD_APPS_DEFAULT)

# Tests are enabled for the whole tree when the canonical switch is on; the
# per-leaf gates already follow it (see the top of this file).
if(MARKOFF_BUILD_TESTS)
    enable_testing()
endif()

# Provider consumer tests are separate executables from markoff_core. They
# link the installed CollabText::Core package through Markoff::CollabTextVNext.
# CMAKE_PREFIX_PATH must include the selected installation.
option(MARKOFF_BUILD_COLLABTEXT_VNEXT_TESTS
    "Build installed-package consumer tests for the CollabText Core adapter"
    ${MARKOFF_BUILD_TESTS})
if(MARKOFF_BUILD_COLLABTEXT_VNEXT_TESTS)
    if(NOT MARKOFF_BUILD_TESTS)
        message(FATAL_ERROR "MARKOFF_BUILD_COLLABTEXT_VNEXT_TESTS requires MARKOFF_BUILD_TESTS=ON")
    endif()
    add_subdirectory(tests/collabtext-vnext)
endif()
