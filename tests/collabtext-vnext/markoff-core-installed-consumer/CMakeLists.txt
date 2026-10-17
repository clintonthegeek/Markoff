cmake_minimum_required(VERSION 3.19)
project(MarkoffCoreInstalledConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED MARKOFF_TARGETS_FILE OR NOT EXISTS "${MARKOFF_TARGETS_FILE}")
    message(FATAL_ERROR
        "Set MARKOFF_TARGETS_FILE to the installed CorbomiteTargets.cmake")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)
find_package(collabtext CONFIG REQUIRED COMPONENTS core)
include("${MARKOFF_TARGETS_FILE}")

add_executable(markoff_core_installed_consumer main.cpp)
target_link_libraries(markoff_core_installed_consumer
    PRIVATE Corbomite::markoff_core)

add_executable(markoff_installed_views_consumer views_main.cpp)
target_link_libraries(markoff_installed_views_consumer PRIVATE
    Corbomite::markoff_core
    Corbomite::markoff_source
    Corbomite::markoff_styled
    Corbomite::markoff_canvas)

if(DEFINED MARKOFF_INSTALL_PREFIX)
    set(_runtime_library_dirs "${MARKOFF_INSTALL_PREFIX}/lib")
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
        list(APPEND _runtime_library_dirs "${_prefix}/lib")
    endforeach()
    list(JOIN _runtime_library_dirs ":" _runtime_library_path)
    set_target_properties(markoff_core_installed_consumer PROPERTIES
        BUILD_RPATH "${_runtime_library_dirs}")
    set_target_properties(markoff_installed_views_consumer PROPERTIES
        BUILD_RPATH "${_runtime_library_dirs}")
endif()

enable_testing()
add_test(NAME markoff_core_installed_consumer
    COMMAND markoff_core_installed_consumer)
add_test(NAME markoff_installed_views_consumer
    COMMAND markoff_installed_views_consumer)
if(DEFINED _runtime_library_path)
    set_tests_properties(markoff_core_installed_consumer
                         markoff_installed_views_consumer PROPERTIES
        ENVIRONMENT "LD_LIBRARY_PATH=${_runtime_library_path}:$ENV{LD_LIBRARY_PATH};QT_QPA_PLATFORM=offscreen")
endif()
