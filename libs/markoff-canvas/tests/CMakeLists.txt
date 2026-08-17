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

add_executable(tst_canvas_task_checkbox tst_canvas_task_checkbox.cpp)
add_test(NAME tst_canvas_task_checkbox COMMAND tst_canvas_task_checkbox)
target_link_libraries(tst_canvas_task_checkbox PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_task_checkbox PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_table tst_canvas_table.cpp)
add_test(NAME tst_canvas_table COMMAND tst_canvas_table)
target_link_libraries(tst_canvas_table PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_table PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_table_selection tst_canvas_table_selection.cpp)
add_test(NAME tst_canvas_table_selection COMMAND tst_canvas_table_selection)
target_link_libraries(tst_canvas_table_selection PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_table_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_table_wrap_nav tst_canvas_table_wrap_nav.cpp)
add_test(NAME tst_canvas_table_wrap_nav COMMAND tst_canvas_table_wrap_nav)
target_link_libraries(tst_canvas_table_wrap_nav PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_table_wrap_nav PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

add_executable(tst_canvas_selection_matches tst_canvas_selection_matches.cpp)
add_test(NAME tst_canvas_selection_matches COMMAND tst_canvas_selection_matches)
target_link_libraries(tst_canvas_selection_matches PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_selection_matches PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_links tst_canvas_links.cpp)
add_test(NAME tst_canvas_links COMMAND tst_canvas_links)
target_link_libraries(tst_canvas_links PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_links PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_click_select tst_canvas_click_select.cpp)
add_test(NAME tst_canvas_click_select COMMAND tst_canvas_click_select)
target_link_libraries(tst_canvas_click_select PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_click_select PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_wheel_zoom tst_canvas_wheel_zoom.cpp)
add_test(NAME tst_canvas_wheel_zoom COMMAND tst_canvas_wheel_zoom)
target_link_libraries(tst_canvas_wheel_zoom PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_wheel_zoom PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_inline_title tst_canvas_inline_title.cpp)
add_test(NAME tst_canvas_inline_title COMMAND tst_canvas_inline_title)
target_link_libraries(tst_canvas_inline_title PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_inline_title PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_format_ops tst_canvas_format_ops.cpp)
add_test(NAME tst_canvas_format_ops COMMAND tst_canvas_format_ops)
target_link_libraries(tst_canvas_format_ops PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_format_ops PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_context_menu tst_canvas_context_menu.cpp)
add_test(NAME tst_canvas_context_menu COMMAND tst_canvas_context_menu)
target_link_libraries(tst_canvas_context_menu PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_context_menu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_layout_width tst_canvas_layout_width.cpp)
add_test(NAME tst_canvas_layout_width COMMAND tst_canvas_layout_width)
target_link_libraries(tst_canvas_layout_width PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_layout_width PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_code_highlight tst_canvas_code_highlight.cpp)
add_test(NAME tst_canvas_code_highlight COMMAND tst_canvas_code_highlight)
target_link_libraries(tst_canvas_code_highlight PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_code_highlight PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_table_ops tst_canvas_table_ops.cpp)
add_test(NAME tst_canvas_table_ops COMMAND tst_canvas_table_ops)
target_link_libraries(tst_canvas_table_ops PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_table_ops PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_math tst_canvas_math.cpp)
add_test(NAME tst_canvas_math COMMAND tst_canvas_math)
target_link_libraries(tst_canvas_math PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_math PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_media_seams tst_canvas_media_seams.cpp)
add_test(NAME tst_canvas_media_seams COMMAND tst_canvas_media_seams)
target_link_libraries(tst_canvas_media_seams PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_media_seams PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_side_content tst_canvas_side_content.cpp)
add_test(NAME tst_canvas_side_content COMMAND tst_canvas_side_content)
target_link_libraries(tst_canvas_side_content PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_side_content PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_folding tst_canvas_folding.cpp)
add_test(NAME tst_canvas_folding COMMAND tst_canvas_folding)
target_link_libraries(tst_canvas_folding PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_folding PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_session_selection tst_canvas_session_selection.cpp)
add_test(NAME tst_canvas_session_selection COMMAND tst_canvas_session_selection)
target_link_libraries(tst_canvas_session_selection PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_session_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_remote_presence tst_canvas_remote_presence.cpp)
add_test(NAME tst_canvas_remote_presence COMMAND tst_canvas_remote_presence)
target_link_libraries(tst_canvas_remote_presence PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_remote_presence PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_concurrency tst_canvas_concurrency.cpp)
add_test(NAME tst_canvas_concurrency COMMAND tst_canvas_concurrency)
target_link_libraries(tst_canvas_concurrency PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_concurrency PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_drag_drop tst_canvas_drag_drop.cpp)
add_test(NAME tst_canvas_drag_drop COMMAND tst_canvas_drag_drop)
target_link_libraries(tst_canvas_drag_drop PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_drag_drop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_editing_command_floor tst_canvas_editing_command_floor.cpp)
add_test(NAME tst_canvas_editing_command_floor COMMAND tst_canvas_editing_command_floor)
target_link_libraries(tst_canvas_editing_command_floor PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_editing_command_floor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_auto_pair tst_canvas_auto_pair.cpp)
add_test(NAME tst_canvas_auto_pair COMMAND tst_canvas_auto_pair)
target_link_libraries(tst_canvas_auto_pair PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_auto_pair PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canvas_p72f tst_canvas_p72f.cpp)
add_test(NAME tst_canvas_p72f COMMAND tst_canvas_p72f)
target_link_libraries(tst_canvas_p72f PRIVATE Qt6::Test Qt6::Widgets markoff_canvas markoff_core)
set_tests_properties(tst_canvas_p72f PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The constitution gate (spec §6, C1–C4) runs as a test so that
# `scripts/run-tests.sh -R canvas` enforces it alongside the leaf's own
# tests. Name carries "canvas" deliberately — that is the -R filter.
add_test(NAME tst_canvas_constitution
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check-constitution.sh)
