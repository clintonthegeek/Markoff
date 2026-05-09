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

qt_add_executable(tst_live_render_e2_autohide_per_kind
    tst_live_render_e2_autohide_per_kind.cpp
)
target_link_libraries(tst_live_render_e2_autohide_per_kind PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_autohide_per_kind
         COMMAND tst_live_render_e2_autohide_per_kind)

qt_add_executable(tst_live_render_e2_autohide_caret_adjacent
    tst_live_render_e2_autohide_caret_adjacent.cpp
)
target_link_libraries(tst_live_render_e2_autohide_caret_adjacent PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_autohide_caret_adjacent
         COMMAND tst_live_render_e2_autohide_caret_adjacent)

qt_add_executable(tst_live_render_e2_autohide_link
    tst_live_render_e2_autohide_link.cpp
)
target_link_libraries(tst_live_render_e2_autohide_link PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_autohide_link
         COMMAND tst_live_render_e2_autohide_link)

qt_add_executable(tst_live_render_e2_autohide_wikilink
    tst_live_render_e2_autohide_wikilink.cpp
)
target_link_libraries(tst_live_render_e2_autohide_wikilink PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_autohide_wikilink
         COMMAND tst_live_render_e2_autohide_wikilink)

qt_add_executable(tst_live_render_e2_autohide_block_prefix
    tst_live_render_e2_autohide_block_prefix.cpp
)
target_link_libraries(tst_live_render_e2_autohide_block_prefix PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_autohide_block_prefix
         COMMAND tst_live_render_e2_autohide_block_prefix)

qt_add_executable(tst_live_render_e2_autohide_nested
    tst_live_render_e2_autohide_nested.cpp
)
target_link_libraries(tst_live_render_e2_autohide_nested PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_autohide_nested
         COMMAND tst_live_render_e2_autohide_nested)

qt_add_executable(tst_live_render_e2_autohide_selection
    tst_live_render_e2_autohide_selection.cpp
)
target_link_libraries(tst_live_render_e2_autohide_selection PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_autohide_selection
         COMMAND tst_live_render_e2_autohide_selection)

qt_add_executable(tst_live_render_e2_autohide_peer_cursor
    tst_live_render_e2_autohide_peer_cursor.cpp
)
target_link_libraries(tst_live_render_e2_autohide_peer_cursor PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_autohide_peer_cursor
         COMMAND tst_live_render_e2_autohide_peer_cursor)

qt_add_executable(tst_live_render_e2_cross_delegate
    tst_live_render_e2_cross_delegate.cpp
)
target_link_libraries(tst_live_render_e2_cross_delegate PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_cross_delegate
         COMMAND tst_live_render_e2_cross_delegate)

qt_add_executable(tst_live_render_e2_nav_arrows
    tst_live_render_e2_nav_arrows.cpp
)
target_link_libraries(tst_live_render_e2_nav_arrows PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_e2_nav_arrows
         COMMAND tst_live_render_e2_nav_arrows)

qt_add_executable(tst_live_render_e2_nav_column_preservation
    tst_live_render_e2_nav_column_preservation.cpp)
target_link_libraries(tst_live_render_e2_nav_column_preservation
    PRIVATE markoff_live Qt6::Test Qt6::Gui)
add_test(NAME tst_live_render_e2_nav_column_preservation
         COMMAND tst_live_render_e2_nav_column_preservation)

qt_add_executable(tst_live_render_e2_nav_home_end
    tst_live_render_e2_nav_home_end.cpp)
target_link_libraries(tst_live_render_e2_nav_home_end
    PRIVATE markoff_live Qt6::Test Qt6::Gui)
add_test(NAME tst_live_render_e2_nav_home_end
         COMMAND tst_live_render_e2_nav_home_end)

qt_add_executable(tst_live_render_e2_nav_word
    tst_live_render_e2_nav_word.cpp)
target_link_libraries(tst_live_render_e2_nav_word
    PRIVATE markoff_live Qt6::Test Qt6::Gui)
add_test(NAME tst_live_render_e2_nav_word
         COMMAND tst_live_render_e2_nav_word)

qt_add_executable(tst_live_render_e2_nav_page
    tst_live_render_e2_nav_page.cpp)
target_link_libraries(tst_live_render_e2_nav_page
    PRIVATE markoff_live Qt6::Test Qt6::Gui)
add_test(NAME tst_live_render_e2_nav_page
         COMMAND tst_live_render_e2_nav_page)

qt_add_executable(tst_live_render_e2_nav_shift_extend
    tst_live_render_e2_nav_shift_extend.cpp)
target_link_libraries(tst_live_render_e2_nav_shift_extend
    PRIVATE markoff_live Qt6::Test Qt6::Gui)
add_test(NAME tst_live_render_e2_nav_shift_extend
         COMMAND tst_live_render_e2_nav_shift_extend)

qt_add_executable(tst_live_render_e2_perf_caret_move
    tst_live_render_e2_perf_caret_move.cpp)
target_link_libraries(tst_live_render_e2_perf_caret_move
    PRIVATE markoff_live Qt6::Test markoff_core)
add_test(NAME tst_live_render_e2_perf_caret_move
         COMMAND tst_live_render_e2_perf_caret_move)

qt_add_executable(tst_live_render_e2_perf_caret_inline
    tst_live_render_e2_perf_caret_inline.cpp)
target_link_libraries(tst_live_render_e2_perf_caret_inline
    PRIVATE markoff_live Qt6::Test)
add_test(NAME tst_live_render_e2_perf_caret_inline
         COMMAND tst_live_render_e2_perf_caret_inline)

qt_add_executable(tst_live_render_clipboard_copy
    tst_live_render_clipboard_copy.cpp)
target_link_libraries(tst_live_render_clipboard_copy
    PRIVATE markoff_live Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_clipboard_copy
         COMMAND tst_live_render_clipboard_copy)

qt_add_executable(tst_live_render_selection_select_all
    tst_live_render_selection_select_all.cpp)
target_link_libraries(tst_live_render_selection_select_all
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_selection_select_all
         COMMAND tst_live_render_selection_select_all)

qt_add_executable(tst_live_render_selection_delete
    tst_live_render_selection_delete.cpp)
target_link_libraries(tst_live_render_selection_delete
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_selection_delete
         COMMAND tst_live_render_selection_delete)

# B3: Session subscription — external setPrimarySelection propagates to view.
qt_add_executable(tst_live_render_session_apply
    tst_live_render_session_apply.cpp)
target_link_libraries(tst_live_render_session_apply
    PRIVATE markoff_live markoff_core Qt6::Core Qt6::Test)
add_test(NAME tst_live_render_session_apply
         COMMAND tst_live_render_session_apply)

# B4: Two bindings share one session — binding A selection visible in binding B.
qt_add_executable(tst_live_render_session_two_bindings
    tst_live_render_session_two_bindings.cpp)
target_link_libraries(tst_live_render_session_two_bindings
    PRIVATE markoff_live markoff_core Qt6::Core Qt6::Test)
add_test(NAME tst_live_render_session_two_bindings
         COMMAND tst_live_render_session_two_bindings)

# B5a: Out-of-range TextAnchor clamps to block length — no crash.
qt_add_executable(tst_live_render_session_clamp
    tst_live_render_session_clamp.cpp)
target_link_libraries(tst_live_render_session_clamp
    PRIVATE markoff_live markoff_core Qt6::Core Qt6::Test)
add_test(NAME tst_live_render_session_clamp
         COMMAND tst_live_render_session_clamp)

# B5b: Selection on a now-deleted block clears gracefully — no crash.
qt_add_executable(tst_live_render_session_orphaned_block
    tst_live_render_session_orphaned_block.cpp)
target_link_libraries(tst_live_render_session_orphaned_block
    PRIVATE markoff_live markoff_core Qt6::Core Qt6::Test)
add_test(NAME tst_live_render_session_orphaned_block
         COMMAND tst_live_render_session_orphaned_block)

# C2: Cut — copy+delete with cutSequenceNumber + recent-cuts cache.
qt_add_executable(tst_live_render_clipboard_cut
    tst_live_render_clipboard_cut.cpp)
target_link_libraries(tst_live_render_clipboard_cut
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_clipboard_cut
         COMMAND tst_live_render_clipboard_cut)

# C3: Paste — flat text/plain fallback path.
qt_add_executable(tst_live_render_clipboard_paste_flat
    tst_live_render_clipboard_paste_flat.cpp)
target_link_libraries(tst_live_render_clipboard_paste_flat
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_clipboard_paste_flat
         COMMAND tst_live_render_clipboard_paste_flat)

