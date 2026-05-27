# D2 test executables. Each tst_* target is added by its respective phase task.
# Keep this file minimal; tasks add their own targets.

qt_add_executable(tst_causal_lww_map tst_causal_lww_map.cpp)
target_link_libraries(tst_causal_lww_map PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_causal_lww_map COMMAND tst_causal_lww_map)

qt_add_executable(tst_block_id tst_block_id.cpp)
target_link_libraries(tst_block_id PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_block_id COMMAND tst_block_id)

qt_add_executable(tst_block_types tst_block_types.cpp)
target_link_libraries(tst_block_types PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_block_types COMMAND tst_block_types)

qt_add_executable(tst_undo_log tst_undo_log.cpp)
target_link_libraries(tst_undo_log PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_undo_log COMMAND tst_undo_log)

qt_add_executable(tst_d2_apply_block_edit tst_d2_apply_block_edit.cpp)
target_link_libraries(tst_d2_apply_block_edit PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_apply_block_edit COMMAND tst_d2_apply_block_edit)

qt_add_executable(tst_d2_apply_structural tst_d2_apply_structural.cpp)
target_link_libraries(tst_d2_apply_structural PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_apply_structural COMMAND tst_d2_apply_structural)

qt_add_executable(tst_d2_undo tst_d2_undo.cpp)
target_link_libraries(tst_d2_undo PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_undo COMMAND tst_d2_undo)

qt_add_executable(tst_d2_signals tst_d2_signals.cpp)
target_link_libraries(tst_d2_signals PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_signals COMMAND tst_d2_signals)

qt_add_executable(tst_d2_sibling_maps tst_d2_sibling_maps.cpp)
target_link_libraries(tst_d2_sibling_maps PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_sibling_maps COMMAND tst_d2_sibling_maps)

qt_add_executable(tst_d2_cmd_decomposition tst_d2_cmd_decomposition.cpp)
target_link_libraries(tst_d2_cmd_decomposition PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_cmd_decomposition COMMAND tst_d2_cmd_decomposition)

qt_add_executable(tst_block_buffer_invariant
    tst_block_buffer_invariant.cpp)
target_link_libraries(tst_block_buffer_invariant PRIVATE
    Qt6::Core Qt6::Test markoff_core)
add_test(NAME tst_block_buffer_invariant
         COMMAND tst_block_buffer_invariant)
set_tests_properties(tst_block_buffer_invariant
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_d2_load tst_d2_load.cpp)
target_link_libraries(tst_d2_load PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_load COMMAND tst_d2_load)

# resetContent must populate D2 per-block state (port-first fix 2026-05-20)
qt_add_executable(tst_d2_reset_content tst_d2_reset_content.cpp)
target_link_libraries(tst_d2_reset_content PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_reset_content COMMAND tst_d2_reset_content)

# Temporary byte-range diagnostic (always passes)
qt_add_executable(tst_d2_byterange_probe tst_d2_byterange_probe.cpp)
target_link_libraries(tst_d2_byterange_probe PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_byterange_probe COMMAND tst_d2_byterange_probe)

# Phase 8: Save
qt_add_executable(tst_d2_save tst_d2_save.cpp)
target_link_libraries(tst_d2_save PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_save COMMAND tst_d2_save)

# Phase 8: Round-trip corpus
qt_add_executable(tst_d2_roundtrip tst_d2_roundtrip.cpp)
target_link_libraries(tst_d2_roundtrip PRIVATE Qt6::Test markoff_core)
target_compile_definitions(tst_d2_roundtrip PRIVATE
    CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/roundtrip/corpus")
add_test(NAME tst_d2_roundtrip COMMAND tst_d2_roundtrip)

# Phase 9: Garbage Collection / WatermarkCoordinator
qt_add_executable(tst_d2_gc tst_d2_gc.cpp)
target_link_libraries(tst_d2_gc PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_gc COMMAND tst_d2_gc)

# Phase 10: InlineParseCache
qt_add_executable(tst_d2_inline_cache tst_d2_inline_cache.cpp)
target_link_libraries(tst_d2_inline_cache PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_inline_cache COMMAND tst_d2_inline_cache)

# Phase 13: Convergence tests (CRDT primitive level — IdList + Buffer)
qt_add_executable(tst_d2_convergence tst_d2_convergence.cpp)
target_link_libraries(tst_d2_convergence PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_convergence COMMAND tst_d2_convergence)

# D3-correction Task 5: per-item ListItem materialization roundtrip (TDD red)
qt_add_executable(tst_d2_list_roundtrip tst_d2_list_roundtrip.cpp)
target_link_libraries(tst_d2_list_roundtrip PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_list_roundtrip COMMAND tst_d2_list_roundtrip)

# D3-correction Task 9: renumberRunStartingAt + insertListItemAfter
qt_add_executable(tst_d2_list_renumber tst_d2_list_renumber.cpp)
target_link_libraries(tst_d2_list_renumber PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_list_renumber COMMAND tst_d2_list_renumber)

# D4 Phase 1: applyFlatEdit primitive
qt_add_executable(tst_d4_apply_flat_edit tst_d4_apply_flat_edit.cpp)
target_link_libraries(tst_d4_apply_flat_edit PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d4_apply_flat_edit COMMAND tst_d4_apply_flat_edit)

# RT2: normalize-on-edit invariant (split newline-runs, no empty blocks)
qt_add_executable(tst_d2_normalize_on_edit tst_d2_normalize_on_edit.cpp)
target_link_libraries(tst_d2_normalize_on_edit PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_normalize_on_edit COMMAND tst_d2_normalize_on_edit)
set_tests_properties(tst_d2_normalize_on_edit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
