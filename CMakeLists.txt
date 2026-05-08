cmake_minimum_required(VERSION 3.19)
project(Markoff VERSION 0.1.0 LANGUAGES CXX)

# Markoff-family libraries (new-foundation branch):
#   - libs/rapidyaml          (ryml::ryml — YAML parser, vendored)
#   - libs/markoff-parser     (Markoff::Parser — tree-sitter Markdown AST + YAML frontmatter)
#   - libs/collabtext         (CRDT text engine, sibling-symlinked from /home/clinton/dev/collabtext)
#   - libs/markoff-core (Markoff::Core — MarkoffDocument + ParsePool + Sessions)
#   - libs/markoff-source (QPlainTextEdit-based source widget)
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

# collabtext — CRDT engine, sibling library symlinked from /home/clinton/dev/collabtext.
# Defensive guard against double-add by parent projects.
if(NOT TARGET collabtext)
    add_subdirectory(libs/collabtext)
endif()

add_subdirectory(libs/markoff-core)
# jkqtmathtext — LaTeX math rendering for Math blocks in markoff-live.
if(NOT TARGET jkqtmathtext)
    add_subdirectory(libs/jkqtmathtext)
endif()
add_subdirectory(libs/markoff-live)
add_subdirectory(libs/markoff-source)
add_subdirectory(apps)

option(MARKOFF_BUILD_TESTS "Build Markoff integration tests" ON)
if(MARKOFF_BUILD_TESTS)
    enable_testing()
endif()
