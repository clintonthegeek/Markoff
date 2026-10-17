cmake_minimum_required(VERSION 3.19)
project(MarkoffCoreInstallHarness LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED MARKOFF_SOURCE_DIR
   OR NOT EXISTS "${MARKOFF_SOURCE_DIR}/libs/markoff-core/CMakeLists.txt")
    message(FATAL_ERROR "Set MARKOFF_SOURCE_DIR to the Markoff source tree")
endif()

set(KDE_INSTALL_LIBDIR lib)
set(KDE_INSTALL_INCLUDEDIR include)
set(KDE_INSTALL_BINDIR bin)
set(MARKOFF_USE_COLLABTEXT_VNEXT ON CACHE BOOL "" FORCE)
set(MARKOFF_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MARKOFF_BUILD_LIVE OFF CACHE BOOL "" FORCE)
set(MARKOFF_BUILD_APPS OFF CACHE BOOL "" FORCE)
set(MARKOFF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MARKOFF_SOURCE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MARKOFF_STYLED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MARKOFF_CANVAS_BUILD_TESTS OFF CACHE BOOL "" FORCE)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)
find_package(collabtext CONFIG REQUIRED COMPONENTS core)

add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/rapidyaml" rapidyaml)
add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/markoff-parser" markoff-parser)
add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/markoff-text-units" markoff-text-units)
add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/markoff-collabtext-vnext"
                 markoff-collabtext-vnext)
add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/markoff-core" markoff-core)
add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/jkqtmathtext" jkqtmathtext)
add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/markoff-source" markoff-source)
add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/markoff-styled" markoff-styled)
add_subdirectory("${MARKOFF_SOURCE_DIR}/libs/markoff-canvas" markoff-canvas)

install(EXPORT CorbomiteTargets
    FILE CorbomiteTargets.cmake
    NAMESPACE Corbomite::
    DESTINATION lib/cmake/MarkoffCoreHarness)
