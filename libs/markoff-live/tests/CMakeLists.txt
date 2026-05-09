# R1C: skeleton. R2: coords, registry, block_model. R3: cursor.

qt_add_executable(tst_live_render_skeleton
    tst_live_render_skeleton.cpp
)
target_link_libraries(tst_live_render_skeleton PRIVATE
    Qt6::Core Qt6::Test markoff_live)
add_test(NAME tst_live_render_skeleton COMMAND tst_live_render_skeleton)

qt_add_executable(tst_live_render_registry
    tst_live_render_registry.cpp
)
target_link_libraries(tst_live_render_registry PRIVATE
    Qt6::Core Qt6::Test markoff_live)
add_test(NAME tst_live_render_registry COMMAND tst_live_render_registry)

qt_add_executable(tst_live_render_coords
    tst_live_render_coords.cpp
)
target_link_libraries(tst_live_render_coords PRIVATE
    Qt6::Core Qt6::Test markoff_live)
add_test(NAME tst_live_render_coords COMMAND tst_live_render_coords)

qt_add_executable(tst_live_render_block_model
    tst_live_render_block_model.cpp
)
target_link_libraries(tst_live_render_block_model PRIVATE
    Qt6::Core Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_block_model COMMAND tst_live_render_block_model)

qt_add_executable(tst_live_render_cursor
    tst_live_render_cursor.cpp
)
target_link_libraries(tst_live_render_cursor PRIVATE
    Qt6::Core Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_cursor COMMAND tst_live_render_cursor)

qt_add_executable(tst_live_render_paragraph_edit
    tst_live_render_paragraph_edit.cpp
)
target_link_libraries(tst_live_render_paragraph_edit PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Widgets Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_paragraph_edit COMMAND tst_live_render_paragraph_edit)

qt_add_executable(tst_live_render_structural
    tst_live_render_structural.cpp
)
target_link_libraries(tst_live_render_structural PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Widgets Qt6::Test
    markoff_live markoff_core)
add_test(NAME tst_live_render_structural COMMAND tst_live_render_structural)

qt_add_executable(tst_live_render_kind_transition
    tst_live_render_kind_transition.cpp
)
target_include_directories(tst_live_render_kind_transition PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(tst_live_render_kind_transition PRIVATE
    Qt6::Core Qt6::Test markoff_live)
add_test(NAME tst_live_render_kind_transition
         COMMAND tst_live_render_kind_transition)

qt_add_executable(tst_live_render_context_menu
    tst_live_render_context_menu.cpp
)
target_link_libraries(tst_live_render_context_menu PRIVATE
    Qt6::Core Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_context_menu COMMAND tst_live_render_context_menu)

qt_add_executable(tst_d5_remote_cursor_render
    tst_d5_remote_cursor_render.cpp
)
target_link_libraries(tst_d5_remote_cursor_render PRIVATE
    Qt6::Core Qt6::Test markoff_live markoff_core)
add_test(NAME tst_d5_remote_cursor_render COMMAND tst_d5_remote_cursor_render)

qt_add_executable(tst_live_render_inline_per_kind
    tst_live_render_inline_per_kind.cpp
)
target_link_libraries(tst_live_render_inline_per_kind PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_per_kind COMMAND tst_live_render_inline_per_kind)

qt_add_executable(tst_live_render_inline_combined
    tst_live_render_inline_combined.cpp
)
target_link_libraries(tst_live_render_inline_combined PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_combined COMMAND tst_live_render_inline_combined)

qt_add_executable(tst_live_render_inline_cross_delegate
    tst_live_render_inline_cross_delegate.cpp
)
target_link_libraries(tst_live_render_inline_cross_delegate PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_cross_delegate COMMAND tst_live_render_inline_cross_delegate)

qt_add_executable(tst_live_render_inline_edge_cases
    tst_live_render_inline_edge_cases.cpp
)
target_link_libraries(tst_live_render_inline_edge_cases PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_edge_cases COMMAND tst_live_render_inline_edge_cases)

qt_add_executable(tst_live_render_inline_typing_perf
    tst_live_render_inline_typing_perf.cpp
)
target_link_libraries(tst_live_render_inline_typing_perf PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_typing_perf
         COMMAND tst_live_render_inline_typing_perf)

qt_add_executable(tst_live_render_e2_zero_width_mechanism
    tst_live_render_e2_zero_width_mechanism.cpp
)
target_link_libraries(tst_live_render_e2_zero_width_mechanism PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_core)
add_test(NAME tst_live_render_e2_zero_width_mechanism
         COMMAND tst_live_render_e2_zero_width_mechanism)

