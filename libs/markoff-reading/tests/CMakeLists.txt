cmake_minimum_required(VERSION 3.19)
project(markoff_reading_tests LANGUAGES CXX)
enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Test Widgets)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

function(add_rv_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    add_test(NAME ${NAME} COMMAND ${NAME})
    target_link_libraries(${NAME} PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Widgets
        markoff_reading
    )
    set_tests_properties(${NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

add_rv_test(tst_readingview_bootstrap)
add_rv_test(tst_readingpipeline_section_split)
add_rv_test(tst_sectionlayout_basic)
add_rv_test(tst_readingview_end_to_end)
add_rv_test(tst_sectionlayout_tables)
add_rv_test(tst_sectionlayout_images)
add_rv_test(tst_sectionlayout_wiki_links)
add_rv_test(tst_sectionlayout_math)
# TODO Phase B: needs real mmdr Rust FFI — stubbed in Phase A.
# add_rv_test(tst_sectionlayout_mermaid)
add_rv_test(tst_section_recycle)
add_rv_test(tst_async_parse)
add_rv_test(tst_frame_budget_constants)
add_rv_test(tst_virtual_scroll)
add_rv_test(tst_heading_fold)
add_rv_test(tst_readingview_linkrenderer)
# TODO Phase B: needs real Corbomite::Storage (MetadataCache / MetadataParser)
# and the real EmbedDepthGuard::placeholderTarget — stubbed in Phase A.
# add_rv_test(tst_readingview_embedrenderer)
# TODO Phase B: needs real mmdr Rust FFI — stubbed in Phase A.
# add_rv_test(tst_readingview_mermaid_registered)
add_rv_test(tst_readingview_math_registered)
add_rv_test(tst_readingview_syntax_registered)
# TODO Phase B: needs real Corbomite::Storage metadata parsing — stubbed in Phase A.
# add_rv_test(tst_readingview_embed_builtins)
add_rv_test(tst_readingview_embed_image_shim)
add_rv_test(tst_readingview_embed_media_stubs)
add_rv_test(tst_reading_markdown_view)
