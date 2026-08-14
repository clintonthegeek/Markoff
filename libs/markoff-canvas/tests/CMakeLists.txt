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

add_executable(tst_canvas_undo tst_canvas_undo.cpp)
add_test(NAME tst_canvas_undo COMMAND tst_canvas_undo)
target_link_libraries(tst_canvas_undo PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_undo PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_selection tst_canvas_selection.cpp)
add_test(NAME tst_canvas_selection COMMAND tst_canvas_selection)
target_link_libraries(tst_canvas_selection PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_kind_transition tst_canvas_kind_transition.cpp)
add_test(NAME tst_canvas_kind_transition COMMAND tst_canvas_kind_transition)
target_link_libraries(tst_canvas_kind_transition PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_kind_transition PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_inline_formatting tst_canvas_inline_formatting.cpp)
add_test(NAME tst_canvas_inline_formatting COMMAND tst_canvas_inline_formatting)
target_link_libraries(tst_canvas_inline_formatting PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_inline_formatting PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_projection tst_canvas_projection.cpp)
add_test(NAME tst_canvas_projection COMMAND tst_canvas_projection)
target_link_libraries(tst_canvas_projection PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_projection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_ime tst_canvas_ime.cpp)
add_test(NAME tst_canvas_ime COMMAND tst_canvas_ime)
target_link_libraries(tst_canvas_ime PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_ime PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_table tst_canvas_table.cpp)
add_test(NAME tst_canvas_table COMMAND tst_canvas_table)
target_link_libraries(tst_canvas_table PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_table PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_table_selection tst_canvas_table_selection.cpp)
add_test(NAME tst_canvas_table_selection COMMAND tst_canvas_table_selection)
target_link_libraries(tst_canvas_table_selection PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_table_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_perf_500 tst_canvas_perf_500.cpp)
add_test(NAME tst_canvas_perf_500 COMMAND tst_canvas_perf_500)
target_link_libraries(tst_canvas_perf_500 PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_perf_500 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_perf_formatted tst_canvas_perf_formatted.cpp)
add_test(NAME tst_canvas_perf_formatted COMMAND tst_canvas_perf_formatted)
target_link_libraries(tst_canvas_perf_formatted PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_perf_formatted PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_view_contract tst_canvas_view_contract.cpp)
add_test(NAME tst_canvas_view_contract COMMAND tst_canvas_view_contract)
target_link_libraries(tst_canvas_view_contract PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_view_contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_find tst_canvas_find.cpp)
add_test(NAME tst_canvas_find COMMAND tst_canvas_find)
target_link_libraries(tst_canvas_find PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_find PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_links tst_canvas_links.cpp)
add_test(NAME tst_canvas_links COMMAND tst_canvas_links)
target_link_libraries(tst_canvas_links PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_links PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_format_ops tst_canvas_format_ops.cpp)
add_test(NAME tst_canvas_format_ops COMMAND tst_canvas_format_ops)
target_link_libraries(tst_canvas_format_ops PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_format_ops PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_context_menu tst_canvas_context_menu.cpp)
add_test(NAME tst_canvas_context_menu COMMAND tst_canvas_context_menu)
target_link_libraries(tst_canvas_context_menu PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_context_menu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The constitution gate (spec §6, C1–C4) runs as a test so that
# `scripts/run-tests.sh -R canvas` enforces it alongside the leaf's own
# tests. Name carries "canvas" deliberately — that is the -R filter.
add_test(NAME tst_canvas_constitution
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check-constitution.sh)
