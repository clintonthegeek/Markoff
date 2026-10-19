# Provider-backed document tests. The retired numeric-replica suite is gone.

# ctest selection labels. The whole suite here is fast and self-checking;
# the manual load benchmark (perf_load_bench) deliberately stays out of ctest
# because it needs an input file and prints numbers instead of asserting.
set_property(DIRECTORY APPEND PROPERTY LABELS unit)

foreach(test_name IN ITEMS
        tst_causal_lww_map tst_block_id tst_block_types
        tst_d2_apply_block_edit tst_d2_apply_structural tst_d2_undo
        tst_d2_sibling_maps tst_d2_save_history)
    qt_add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE Qt6::Test markoff_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
