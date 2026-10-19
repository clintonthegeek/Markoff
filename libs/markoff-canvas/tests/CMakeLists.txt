cmake_minimum_required(VERSION 3.19)
# ctest selection labels. The whole suite here is fast and self-checking;
# the manual load benchmark (perf_load_bench) deliberately stays out of ctest
# because it needs an input file and prints numbers instead of asserting.
set_property(DIRECTORY APPEND PROPERTY LABELS unit)

find_package(Qt6 6.8 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tst_canvas_vnext_session_positions
    tst_canvas_vnext_session_positions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/markoff-collab-testapp/CollabConsumer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/markoff-collab-testapp/EphemeralPresence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/markoff-collab-testapp/InMemoryTransport.cpp)
target_link_libraries(tst_canvas_vnext_session_positions
    PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core
            Markoff::CollabTextVNext)
target_include_directories(tst_canvas_vnext_session_positions PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../markoff-collabtext-vnext/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/markoff-collab-testapp)
add_test(NAME tst_canvas_vnext_session_positions
    COMMAND tst_canvas_vnext_session_positions)
set_tests_properties(tst_canvas_vnext_session_positions
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Rendering harness: real offscreen View + painted-pixel checks. Restores the
# coverage the A13 cut deleted (math/table/mermaid/image render paths).
add_executable(tst_canvas_render tst_canvas_render.cpp)
target_link_libraries(tst_canvas_render
    PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core Markoff::Embed)
add_test(NAME tst_canvas_render COMMAND tst_canvas_render)
set_tests_properties(tst_canvas_render
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The constitution gate (spec §6, C1–C4) runs as a test so that
# `scripts/run-tests.sh -R canvas` enforces it alongside the leaf's own
# tests. Name carries "canvas" deliberately — that is the -R filter.
add_test(NAME tst_canvas_constitution
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check-constitution.sh)
