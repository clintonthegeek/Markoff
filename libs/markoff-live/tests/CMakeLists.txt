# R1C: skeleton. R2: coords, registry, block_model. R3: cursor.

qt_add_executable(tst_kind_dispatch
    tst_kind_dispatch.cpp
)
target_include_directories(tst_kind_dispatch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(tst_kind_dispatch PRIVATE
    Qt6::Core Qt6::Test markoff_live)
add_test(NAME tst_kind_dispatch COMMAND tst_kind_dispatch)

# E4 A5: Table descriptor in BlockKindRegistry
qt_add_executable(tst_block_kind_registry_table
    tst_block_kind_registry_table.cpp
)
target_link_libraries(tst_block_kind_registry_table PRIVATE
    Qt6::Core Qt6::Test markoff_live)
add_test(NAME tst_block_kind_registry_table COMMAND tst_block_kind_registry_table)
set_tests_properties(tst_block_kind_registry_table PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_live_find_adapter
    tst_live_find_adapter.cpp
)
target_include_directories(tst_live_find_adapter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(tst_live_find_adapter PRIVATE
    Qt6::Core Qt6::Test markoff_live)
add_test(NAME tst_live_find_adapter COMMAND tst_live_find_adapter)

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
target_include_directories(tst_live_render_block_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME tst_live_render_block_model COMMAND tst_live_render_block_model)

qt_add_executable(tst_live_render_cursor
    tst_live_render_cursor.cpp
)
target_link_libraries(tst_live_render_cursor PRIVATE
    Qt6::Core Qt6::Test markoff_live markoff_core)
target_include_directories(tst_live_render_cursor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
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

qt_add_executable(tst_live_render_setext_e2e
    tst_live_render_setext_e2e.cpp
)
target_link_libraries(tst_live_render_setext_e2e PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Widgets Qt6::Test
    markoff_live markoff_core)
add_test(NAME tst_live_render_setext_e2e
         COMMAND tst_live_render_setext_e2e)

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

# QML-integration companion: ctrl_home/ctrl_end lands-cursor assertions
# require a registered delegate (focus-chokepoint refactor). See header
# comment in the .cpp for the rationale.
qt_add_executable(tst_live_render_e2_nav_home_end_qml
    tst_live_render_e2_nav_home_end_qml.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_e2_nav_home_end_qml PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_e2_nav_home_end_qml
         COMMAND tst_live_render_e2_nav_home_end_qml)
set_tests_properties(tst_live_render_e2_nav_home_end_qml
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

# Phase E tier-4c: 7-slot invariant gate — LiveCursorState canonical store for
# cursor position and selection anchor (spec §5.6).
qt_add_executable(tst_live_render_selection_cursor_unification
    tst_live_render_selection_cursor_unification.cpp)
target_link_libraries(tst_live_render_selection_cursor_unification
    PRIVATE markoff_live markoff_core Qt6::Core Qt6::Test)
add_test(NAME tst_live_render_selection_cursor_unification
         COMMAND tst_live_render_selection_cursor_unification)

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

# C4: Paste — structured fast-path (local cut→paste via x-markoff-blocks).
qt_add_executable(tst_live_render_clipboard_paste_structured
    tst_live_render_clipboard_paste_structured.cpp)
target_link_libraries(tst_live_render_clipboard_paste_structured
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_clipboard_paste_structured
         COMMAND tst_live_render_clipboard_paste_structured)

# C5: Paste — foreign-Markoff-MIME paste routes through structured path
#     without ID reuse.
qt_add_executable(tst_live_render_clipboard_paste_foreign_markoff
    tst_live_render_clipboard_paste_foreign_markoff.cpp)
target_link_libraries(tst_live_render_clipboard_paste_foreign_markoff
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_clipboard_paste_foreign_markoff
         COMMAND tst_live_render_clipboard_paste_foreign_markoff)

# C6: Round-trip — cut/paste cycle preserves content.
qt_add_executable(tst_live_render_clipboard_round_trip
    tst_live_render_clipboard_round_trip.cpp)
target_link_libraries(tst_live_render_clipboard_round_trip
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_clipboard_round_trip
         COMMAND tst_live_render_clipboard_round_trip)

# E2.6 dogfood: paste-kind-roundtrip investigation — verifies copy+paste
# preserves block kinds (Heading/Paragraph/ListItem) across the structured
# paste path. Treat failures as the *current* behavior, not a regression.
qt_add_executable(tst_live_render_paste_kind_roundtrip
    tst_live_render_paste_kind_roundtrip.cpp)
target_link_libraries(tst_live_render_paste_kind_roundtrip
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_paste_kind_roundtrip
         COMMAND tst_live_render_paste_kind_roundtrip)

# D2: LiveActionController enabled-state: paste/cut/copy enable/disable correctly.
qt_add_executable(tst_live_render_actions_enabled_state
    tst_live_render_actions_enabled_state.cpp)
target_link_libraries(tst_live_render_actions_enabled_state
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_actions_enabled_state
         COMMAND tst_live_render_actions_enabled_state)

# D3: LiveActionController dispatch: trigger propagates to clipboard/selection/signals.
qt_add_executable(tst_live_render_actions_dispatch
    tst_live_render_actions_dispatch.cpp)
target_link_libraries(tst_live_render_actions_dispatch
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_actions_dispatch
         COMMAND tst_live_render_actions_dispatch)

# D4: Clipboard-changed signal enables paste action reactively.
qt_add_executable(tst_live_render_clipboard_changed_updates_paste
    tst_live_render_clipboard_changed_updates_paste.cpp)
target_link_libraries(tst_live_render_clipboard_changed_updates_paste
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_clipboard_changed_updates_paste
         COMMAND tst_live_render_clipboard_changed_updates_paste)

# E1: LiveFormatController — bold single-block wrapping.
qt_add_executable(tst_live_render_format_bold_single_block
    tst_live_render_format_bold_single_block.cpp)
target_link_libraries(tst_live_render_format_bold_single_block
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_format_bold_single_block
         COMMAND tst_live_render_format_bold_single_block)

# E2: LiveFormatController — italic single-block wrapping.
qt_add_executable(tst_live_render_format_italic_single_block
    tst_live_render_format_italic_single_block.cpp)
target_link_libraries(tst_live_render_format_italic_single_block
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_format_italic_single_block
         COMMAND tst_live_render_format_italic_single_block)

# Strikethrough + inline-code togglers (2026-05-21 toolbar restoration).
qt_add_executable(tst_live_render_format_strike_inline_code
    tst_live_render_format_strike_inline_code.cpp)
target_link_libraries(tst_live_render_format_strike_inline_code
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_format_strike_inline_code
         COMMAND tst_live_render_format_strike_inline_code)

# Heading-level dispatch (2026-05-21 Phase B).
qt_add_executable(tst_live_render_format_heading_level
    tst_live_render_format_heading_level.cpp)
target_link_libraries(tst_live_render_format_heading_level
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_format_heading_level
         COMMAND tst_live_render_format_heading_level)

# Heading action-trigger path (2026-05-21 dogfood). Distinguishes the
# direct C++ call from the consumer-driven QAction::trigger path.
qt_add_executable(tst_live_render_heading_action_trigger
    tst_live_render_heading_action_trigger.cpp)
target_link_libraries(tst_live_render_heading_action_trigger
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_heading_action_trigger
         COMMAND tst_live_render_heading_action_trigger)
set_tests_properties(tst_live_render_heading_action_trigger
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# E3: LiveFormatController — link insert (empty + nonempty selection).
qt_add_executable(tst_live_render_format_link_single_block
    tst_live_render_format_link_single_block.cpp)
target_link_libraries(tst_live_render_format_link_single_block
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_format_link_single_block
         COMMAND tst_live_render_format_link_single_block)

# E4: LiveFormatController — idempotent toggle (bold/italic round-trip).
qt_add_executable(tst_live_render_format_idempotent
    tst_live_render_format_idempotent.cpp)
target_link_libraries(tst_live_render_format_idempotent
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_format_idempotent
         COMMAND tst_live_render_format_idempotent)

# E5: LiveFormatController — multi-block independent wrapping.
qt_add_executable(tst_live_render_format_multi_block
    tst_live_render_format_multi_block.cpp)
target_link_libraries(tst_live_render_format_multi_block
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_format_multi_block
         COMMAND tst_live_render_format_multi_block)

# H1: Empty-doc focus — one paragraph block on empty load; flat edit lands.
qt_add_executable(tst_live_render_empty_doc_focus
    tst_live_render_empty_doc_focus.cpp)
target_link_libraries(tst_live_render_empty_doc_focus
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_empty_doc_focus
         COMMAND tst_live_render_empty_doc_focus)

# H2: Empty-doc kind-transition — "# Heading" edit promotes block kind.
qt_add_executable(tst_live_render_empty_doc_kind_transition
    tst_live_render_empty_doc_kind_transition.cpp)
target_link_libraries(tst_live_render_empty_doc_kind_transition
    PRIVATE markoff_live markoff_core Qt6::Test)
add_test(NAME tst_live_render_empty_doc_kind_transition
         COMMAND tst_live_render_empty_doc_kind_transition)

# H3: Cut-to-empty — selectAll + cut leaves one empty paragraph.
qt_add_executable(tst_live_render_cut_to_empty
    tst_live_render_cut_to_empty.cpp)
target_link_libraries(tst_live_render_cut_to_empty
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_cut_to_empty
         COMMAND tst_live_render_cut_to_empty)

# I1: Bulk-paste perf bench — 1000-block paste under 200 ms.
qt_add_executable(tst_live_render_e2_5_perf_bulk_paste
    tst_live_render_e2_5_perf_bulk_paste.cpp)
target_link_libraries(tst_live_render_e2_5_perf_bulk_paste
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Widgets)
add_test(NAME tst_live_render_e2_5_perf_bulk_paste
         COMMAND tst_live_render_e2_5_perf_bulk_paste)

# E2.6: fontScale property on LiveListModelBinding.
qt_add_executable(tst_live_render_font_scale_property
    tst_live_render_font_scale_property.cpp)
target_link_libraries(tst_live_render_font_scale_property PRIVATE
    Qt6::Core Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_font_scale_property
         COMMAND tst_live_render_font_scale_property)

# E2.6: zoom QActions on LiveActionController.
qt_add_executable(tst_live_render_font_scale_actions
    tst_live_render_font_scale_actions.cpp)
target_link_libraries(tst_live_render_font_scale_actions PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_font_scale_actions
         COMMAND tst_live_render_font_scale_actions)
set_tests_properties(tst_live_render_font_scale_actions
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# E2.6: themeToggleRequested signal on LiveActionController.
qt_add_executable(tst_live_render_theme_toggle_signal
    tst_live_render_theme_toggle_signal.cpp)
target_link_libraries(tst_live_render_theme_toggle_signal PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_theme_toggle_signal
         COMMAND tst_live_render_theme_toggle_signal)
set_tests_properties(tst_live_render_theme_toggle_signal
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# E2.6: fontScale propagation through InlineHighlighter.
qt_add_executable(tst_live_render_inline_highlighter_font_scale
    tst_live_render_inline_highlighter_font_scale.cpp)
target_link_libraries(tst_live_render_inline_highlighter_font_scale PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_highlighter_font_scale
         COMMAND tst_live_render_inline_highlighter_font_scale)
set_tests_properties(tst_live_render_inline_highlighter_font_scale
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# E2.6 dogfood remediation: dark-toggle propagation. Pins that
# `LiveListModelBinding::setTheme` swaps internal buffers so the
# `theme()` pointer changes on every set — necessary for QML's binding
# system to re-write downstream `InlineHighlighterAttached.theme` and
# trigger a rehighlight on Ctrl+Shift+D.
qt_add_executable(tst_live_render_theme_toggle_propagation
    tst_live_render_theme_toggle_propagation.cpp)
target_link_libraries(tst_live_render_theme_toggle_propagation PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test markoff_live markoff_core
    markoff_liveplugin markoff_liveplugin_init)
add_test(NAME tst_live_render_theme_toggle_propagation
         COMMAND tst_live_render_theme_toggle_propagation)
set_tests_properties(tst_live_render_theme_toggle_propagation
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Queue #3: QML integration harness. Loads production Main.qml via the
# markoff-live-app-internal STATIC library; runs offscreen.
qt_add_executable(tst_live_render_qml_integration
    tst_live_render_qml_integration.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_qml_integration PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_qml_integration
         COMMAND tst_live_render_qml_integration)
set_tests_properties(tst_live_render_qml_integration
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# L1 from `docs/specs/2026-05-21-textedit-interface-audit.md` — cross-block
# selection collapse on mutating keys.
qt_add_executable(tst_live_render_cross_block_mutating_key
    tst_live_render_cross_block_mutating_key.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_cross_block_mutating_key PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_cross_block_mutating_key
         COMMAND tst_live_render_cross_block_mutating_key)
set_tests_properties(tst_live_render_cross_block_mutating_key
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# B1 buffer convention: interactive contract tests driven through the
# realistic-input harness. Verifies that soft-break '\n' survives split+merge
# as content, and that multi-block paste produces no trailing terminators.
# Spec: docs/specs/2026-05-18-b1-buffer-convention-design.md §5.2
qt_add_executable(tst_block_buffer_interactive
    tst_block_buffer_interactive.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_block_buffer_interactive PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_block_buffer_interactive
         COMMAND tst_block_buffer_interactive)
set_tests_properties(tst_block_buffer_interactive
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_live_render_focus_chokepoint_invariant
    tst_live_render_focus_chokepoint_invariant.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_focus_chokepoint_invariant PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_focus_chokepoint_invariant
         COMMAND tst_live_render_focus_chokepoint_invariant)
set_tests_properties(tst_live_render_focus_chokepoint_invariant
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_live_render_cursor_typing_invariant
    tst_live_render_cursor_typing_invariant.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_cursor_typing_invariant PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_cursor_typing_invariant
         COMMAND tst_live_render_cursor_typing_invariant)
set_tests_properties(tst_live_render_cursor_typing_invariant
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_live_render_focus_after_enter_at_paragraph_end
    tst_live_render_focus_after_enter_at_paragraph_end.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_focus_after_enter_at_paragraph_end PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_focus_after_enter_at_paragraph_end
         COMMAND tst_live_render_focus_after_enter_at_paragraph_end)
set_tests_properties(tst_live_render_focus_after_enter_at_paragraph_end
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_live_render_focus_after_heading_demote_via_hash_deletion
    tst_live_render_focus_after_heading_demote_via_hash_deletion.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_focus_after_heading_demote_via_hash_deletion PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_focus_after_heading_demote_via_hash_deletion
         COMMAND tst_live_render_focus_after_heading_demote_via_hash_deletion)
set_tests_properties(tst_live_render_focus_after_heading_demote_via_hash_deletion
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---- QML-integration companions for chokepoint-affected sub-tests ----
# These five files were spun out of their respective unit test files when
# the focus-chokepoint refactor (LiveCursorState::establishFocus) made
# direct-call cursor assertions infeasible. The originals retain
# return-value / descriptor-data assertions; cursor-landing assertions
# live here, exercised through the real QML window.

foreach(_qmlcompanion
    tst_live_render_cursor_qml
    tst_live_render_structural_qml
    tst_live_render_e2_nav_arrows_qml
    tst_live_render_e2_nav_word_qml
    tst_live_render_e2_nav_shift_extend_qml
    tst_live_render_cross_block_drag_selection_qml
    tst_live_render_hit_test_qml
    tst_live_render_ctrl_backspace_boundary_qml
    tst_live_render_ctrl_shift_word_extend_qml
    tst_live_render_middle_click_paste_qml
    tst_live_render_drop_text_qml
    tst_live_render_ime_composition_qml
    tst_live_render_table_parsing
    tst_live_render_table_cell_edit)
    qt_add_executable(${_qmlcompanion}
        ${_qmlcompanion}.cpp
        QmlIntegrationFixture.h
        QmlIntegrationFixture.cpp)
    target_link_libraries(${_qmlcompanion} PRIVATE
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
        Qt6::Widgets Qt6::Test
        markoff_live markoff_core
        markoff-live-app-internal
        markoff-live-app-internalplugin
        markoff-live-app-internalplugin_init)
    add_test(NAME ${_qmlcompanion} COMMAND ${_qmlcompanion})
    set_tests_properties(${_qmlcompanion}
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

qt_add_executable(tst_live_cursor_state_chokepoint
    tst_live_cursor_state_chokepoint.cpp
)
target_link_libraries(tst_live_cursor_state_chokepoint
    PRIVATE markoff_live Qt6::Test Qt6::Quick)
add_test(NAME tst_live_cursor_state_chokepoint
         COMMAND tst_live_cursor_state_chokepoint)
set_tests_properties(tst_live_cursor_state_chokepoint
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")


# Tier-3 §6.1 invariant: TextEdit QQuickItem identity preserved across
# within-class kind transitions (paragraph↔heading, paragraph→list-item).
# Lands RED — turns green once Task 7 switches BlockKey to delegateClass.
qt_add_executable(tst_live_render_kind_transition_invariant
    tst_live_render_kind_transition_invariant.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
    LiveRealisticInputHarness.h
)
target_link_libraries(tst_live_render_kind_transition_invariant PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_kind_transition_invariant
         COMMAND tst_live_render_kind_transition_invariant)
set_tests_properties(tst_live_render_kind_transition_invariant
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# C4: activateLinkAt Q_INVOKABLE — wikilink/external/noop/image dispatch.
qt_add_executable(tst_live_link_activation
    tst_live_link_activation.cpp
    RecordingLinkService.cpp
)
target_link_libraries(tst_live_link_activation PRIVATE
    Qt6::Core Qt6::Test
    markoff_live markoff_core)
add_test(NAME tst_live_link_activation
         COMMAND tst_live_link_activation)

# C5: hoverLinkAt / clearLinkHover — enter/same-span/cross-link/clear transitions.
qt_add_executable(tst_live_link_hover
    tst_live_link_hover.cpp
    RecordingLinkService.cpp
)
target_link_libraries(tst_live_link_hover PRIVATE
    Qt6::Core Qt6::Test
    markoff_live markoff_core)
add_test(NAME tst_live_link_hover
         COMMAND tst_live_link_hover)

# D1: QML-integration test for Ctrl+click TapHandler in
# UnifiedInlineTextDelegate. Exercises the production callsite
# (QML → activateLinkAt) per INVARIANTS.md #4 and #5.
qt_add_executable(tst_live_link_qml_integration
    tst_live_link_qml_integration.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
    RecordingLinkService.cpp
)
target_link_libraries(tst_live_link_qml_integration PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_link_qml_integration
         COMMAND tst_live_link_qml_integration)
set_tests_properties(tst_live_link_qml_integration
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Spec §8.5 retirement-evidence shape tests (shell scripts, CI guards)
add_test(NAME tst_live_render_no_qt_calllater_in_focus_path
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_no_qt_calllater_in_focus_path.sh
                 ${CMAKE_SOURCE_DIR})
add_test(NAME tst_live_render_focus_path_exits_through_chokepoint
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_focus_path_through_chokepoint.sh
                 ${CMAKE_SOURCE_DIR})
