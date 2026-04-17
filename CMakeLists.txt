cmake_minimum_required(VERSION 3.19)
project(Markoff VERSION 0.1.0 LANGUAGES CXX)

# Markoff-family libraries:
#   - libs/rapidyaml       (ryml::ryml — YAML parser, vendored from biojppm/rapidyaml)
#   - libs/markoff-parser  (MarkoffParser::MarkoffParser — tree-sitter Markdown AST + YAML frontmatter)
#   - libs/markoff         (Markoff::Markoff — QGraphicsView-based Markdown editor widget)
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

# rapidyaml uses exceptions in its default callback — required for our
# error-propagation contract in MarkoffParser.
set(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS ON CACHE BOOL "" FORCE)

add_subdirectory(libs/rapidyaml)
add_subdirectory(libs/markoff-parser)
add_subdirectory(libs/markoff)

# Markoff's own integration tests. Parent projects may skip these by setting
# MARKOFF_BUILD_TESTS=OFF before add_subdirectory().
option(MARKOFF_BUILD_TESTS "Build Markoff integration tests" ON)
if(MARKOFF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/markoff)
endif()
