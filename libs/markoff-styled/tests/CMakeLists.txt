cmake_minimum_required(VERSION 3.19)
find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Test binaries added in subsequent plan tasks.

add_executable(tst_styled_editor_construction tst_styled_editor_construction.cpp)
add_test(NAME tst_styled_editor_construction COMMAND tst_styled_editor_construction)
target_link_libraries(tst_styled_editor_construction
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_editor_construction
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_styled_binding_roundtrip tst_styled_binding_roundtrip.cpp)
add_test(NAME tst_styled_binding_roundtrip COMMAND tst_styled_binding_roundtrip)
target_link_libraries(tst_styled_binding_roundtrip
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_binding_roundtrip
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_styled_block_formats tst_styled_block_formats.cpp)
add_test(NAME tst_styled_block_formats COMMAND tst_styled_block_formats)
target_link_libraries(tst_styled_block_formats
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_block_formats
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_styled_inline_formats tst_styled_inline_formats.cpp)
add_test(NAME tst_styled_inline_formats COMMAND tst_styled_inline_formats)
target_link_libraries(tst_styled_inline_formats
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_inline_formats
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_styled_link_interaction tst_styled_link_interaction.cpp)
add_test(NAME tst_styled_link_interaction COMMAND tst_styled_link_interaction)
target_link_libraries(tst_styled_link_interaction
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
target_include_directories(tst_styled_link_interaction
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_styled_link_interaction
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_styled_delimiter_visibility tst_styled_delimiter_visibility.cpp)
add_test(NAME tst_styled_delimiter_visibility COMMAND tst_styled_delimiter_visibility)
target_link_libraries(tst_styled_delimiter_visibility
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_delimiter_visibility
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
