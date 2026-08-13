cmake_minimum_required(VERSION 3.19)
find_package(Qt6 6.8 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tst_canvas_render tst_canvas_render.cpp)
add_test(NAME tst_canvas_render COMMAND tst_canvas_render)
target_link_libraries(tst_canvas_render PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_typing tst_canvas_typing.cpp)
add_test(NAME tst_canvas_typing COMMAND tst_canvas_typing)
target_link_libraries(tst_canvas_typing PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_typing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_structural tst_canvas_structural.cpp)
add_test(NAME tst_canvas_structural COMMAND tst_canvas_structural)
target_link_libraries(tst_canvas_structural PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_structural PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The constitution gate (spec §6, C1–C4) runs as a test so that
# `scripts/run-tests.sh -R canvas` enforces it alongside the leaf's own
# tests. Name carries "canvas" deliberately — that is the -R filter.
add_test(NAME tst_canvas_constitution
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check-constitution.sh)
