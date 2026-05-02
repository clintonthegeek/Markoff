# R1C provides one trivial test that proves the test infrastructure is
# alive. R2 onwards replaces this with the per-layer test executables
# named in spec §10.2 (tst_live_render_coords, tst_live_render_block_model,
# tst_live_render_cursor, …).

qt_add_executable(tst_live_render_skeleton
    tst_live_render_skeleton.cpp
)

target_link_libraries(tst_live_render_skeleton PRIVATE
    Qt6::Core
    Qt6::Test
    markoff_live_render
)

add_test(NAME tst_live_render_skeleton COMMAND tst_live_render_skeleton)
