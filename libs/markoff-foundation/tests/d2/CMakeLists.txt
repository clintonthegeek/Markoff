# D2 test executables. Each tst_* target is added by its respective phase task.
# Keep this file minimal; tasks add their own targets.

qt_add_executable(tst_causal_lww_map tst_causal_lww_map.cpp)
target_link_libraries(tst_causal_lww_map PRIVATE Qt6::Test markoff_foundation)
add_test(NAME tst_causal_lww_map COMMAND tst_causal_lww_map)

qt_add_executable(tst_block_id tst_block_id.cpp)
target_link_libraries(tst_block_id PRIVATE Qt6::Test markoff_foundation)
add_test(NAME tst_block_id COMMAND tst_block_id)

qt_add_executable(tst_block_types tst_block_types.cpp)
target_link_libraries(tst_block_types PRIVATE Qt6::Test markoff_foundation)
add_test(NAME tst_block_types COMMAND tst_block_types)

qt_add_executable(tst_undo_log tst_undo_log.cpp)
target_link_libraries(tst_undo_log PRIVATE Qt6::Test markoff_foundation)
add_test(NAME tst_undo_log COMMAND tst_undo_log)
