# R1C: skeleton. R2: coords, registry, block_model. R3: cursor.

qt_add_executable(tst_live_render_skeleton
    tst_live_render_skeleton.cpp
)
target_link_libraries(tst_live_render_skeleton PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_skeleton COMMAND tst_live_render_skeleton)

qt_add_executable(tst_live_render_registry
    tst_live_render_registry.cpp
)
target_link_libraries(tst_live_render_registry PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_registry COMMAND tst_live_render_registry)

qt_add_executable(tst_live_render_coords
    tst_live_render_coords.cpp
)
target_link_libraries(tst_live_render_coords PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_coords COMMAND tst_live_render_coords)

qt_add_executable(tst_live_render_block_model
    tst_live_render_block_model.cpp
)
target_link_libraries(tst_live_render_block_model PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_block_model COMMAND tst_live_render_block_model)

qt_add_executable(tst_live_render_cursor
    tst_live_render_cursor.cpp
)
target_link_libraries(tst_live_render_cursor PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_cursor COMMAND tst_live_render_cursor)

qt_add_executable(tst_live_render_paragraph_edit
    tst_live_render_paragraph_edit.cpp
)
target_link_libraries(tst_live_render_paragraph_edit PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Widgets Qt6::Test markoff_live_render markoff_foundation)
add_test(NAME tst_live_render_paragraph_edit COMMAND tst_live_render_paragraph_edit)
