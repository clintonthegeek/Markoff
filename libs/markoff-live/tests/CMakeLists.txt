cmake_minimum_required(VERSION 3.19)
project(markoff_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_executable(tst_markoff_selection tst_selection.cpp)
add_test(NAME tst_markoff_selection COMMAND tst_markoff_selection)
target_link_libraries(tst_markoff_selection PRIVATE Qt6::Test markoff_live)
target_include_directories(tst_markoff_selection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_theme tst_theme.cpp)
add_test(NAME tst_markoff_theme COMMAND tst_markoff_theme)
target_link_libraries(tst_markoff_theme PRIVATE Qt6::Test markoff_live)
set_tests_properties(tst_markoff_theme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_resourceprovider tst_resourceprovider.cpp)
add_test(NAME tst_markoff_resourceprovider COMMAND tst_markoff_resourceprovider)
target_link_libraries(tst_markoff_resourceprovider PRIVATE Qt6::Test markoff_live)
set_tests_properties(tst_markoff_resourceprovider PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_inline_math tst_inline_math.cpp)
add_test(NAME tst_markoff_inline_math COMMAND tst_markoff_inline_math)
target_link_libraries(tst_markoff_inline_math PRIVATE Qt6::Test Qt6::Widgets markoff_live KF6::SyntaxHighlighting)
target_include_directories(tst_markoff_inline_math PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_inline_math PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_editor_formatting tst_editor_formatting.cpp)
add_test(NAME tst_markoff_editor_formatting COMMAND tst_markoff_editor_formatting)
target_link_libraries(tst_markoff_editor_formatting PRIVATE Qt6::Test Qt6::Widgets markoff_live)
set_tests_properties(tst_markoff_editor_formatting PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_set_heading_actions tst_set_heading_actions.cpp)
add_test(NAME tst_markoff_set_heading_actions COMMAND tst_markoff_set_heading_actions)
target_link_libraries(tst_markoff_set_heading_actions PRIVATE Qt6::Test Qt6::Widgets markoff_live)
set_tests_properties(tst_markoff_set_heading_actions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_editor_cursor_in_table tst_editor_cursor_in_table.cpp)
add_test(NAME tst_markoff_editor_cursor_in_table COMMAND tst_markoff_editor_cursor_in_table)
target_link_libraries(tst_markoff_editor_cursor_in_table PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_editor_cursor_in_table PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_editor_cursor_in_table PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_editor_context_classifier tst_editor_context_classifier.cpp)
add_test(NAME tst_markoff_editor_context_classifier COMMAND tst_markoff_editor_context_classifier)
target_link_libraries(tst_markoff_editor_context_classifier PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_editor_context_classifier PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_editor_context_classifier PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_editor_context tst_editor_context.cpp)
add_test(NAME tst_markoff_editor_context COMMAND tst_markoff_editor_context)
target_link_libraries(tst_markoff_editor_context PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_editor_context PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_editor_context PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_undo_grouping tst_undo_grouping.cpp)
add_test(NAME tst_markoff_undo_grouping COMMAND tst_markoff_undo_grouping)
target_link_libraries(tst_markoff_undo_grouping PRIVATE Qt6::Test Qt6::Widgets markoff_live)
set_tests_properties(tst_markoff_undo_grouping PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_folding_model tst_folding_model.cpp)
add_test(NAME tst_markoff_folding_model COMMAND tst_markoff_folding_model)
target_link_libraries(tst_markoff_folding_model PRIVATE Qt6::Test markoff_live)
target_include_directories(tst_markoff_folding_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_folding_model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_fold_persistence tst_fold_persistence.cpp)
add_test(NAME tst_markoff_fold_persistence COMMAND tst_markoff_fold_persistence)
target_link_libraries(tst_markoff_fold_persistence PRIVATE Qt6::Test markoff_live)
target_include_directories(tst_markoff_fold_persistence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_fold_persistence PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_folding_reconcile tst_folding_reconcile.cpp)
add_test(NAME tst_markoff_folding_reconcile COMMAND tst_markoff_folding_reconcile)
target_link_libraries(tst_markoff_folding_reconcile PRIVATE Qt6::Test markoff_live)
target_include_directories(tst_markoff_folding_reconcile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_folding_reconcile PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_folding_integration tst_folding_integration.cpp)
add_test(NAME tst_markoff_folding_integration COMMAND tst_markoff_folding_integration)
target_link_libraries(tst_markoff_folding_integration PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_folding_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_folding_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_folding_tables tst_folding_tables.cpp)
add_test(NAME tst_markoff_folding_tables COMMAND tst_markoff_folding_tables)
target_link_libraries(tst_markoff_folding_tables PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_folding_tables PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_folding_tables PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_fold_gutter tst_fold_gutter.cpp)
add_test(NAME tst_markoff_fold_gutter COMMAND tst_markoff_fold_gutter)
target_link_libraries(tst_markoff_fold_gutter PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_fold_gutter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_fold_gutter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_scroll_position tst_markoff_scroll_position.cpp)
add_test(NAME tst_markoff_scroll_position COMMAND tst_markoff_scroll_position)
target_link_libraries(tst_markoff_scroll_position PRIVATE Qt6::Test Qt6::Widgets markoff_live)
set_tests_properties(tst_markoff_scroll_position PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_global_coordinates tst_global_coordinates.cpp)
add_test(NAME tst_markoff_global_coordinates COMMAND tst_markoff_global_coordinates)
target_link_libraries(tst_markoff_global_coordinates PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_global_coordinates PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_global_coordinates PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_table_serializer tst_table_serializer.cpp)
add_test(NAME tst_markoff_table_serializer COMMAND tst_markoff_table_serializer)
target_link_libraries(tst_markoff_table_serializer PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_table_serializer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_serializer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_table_converter tst_table_converter.cpp)
add_test(NAME tst_markoff_table_converter COMMAND tst_markoff_table_converter)
target_link_libraries(tst_markoff_table_converter PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_table_converter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_converter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_table_navigation tst_table_navigation.cpp)
add_test(NAME tst_markoff_table_navigation COMMAND tst_markoff_table_navigation)
target_link_libraries(tst_markoff_table_navigation PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_table_navigation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_navigation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_table_diagnostics tst_table_diagnostics.cpp)
add_test(NAME tst_markoff_table_diagnostics COMMAND tst_markoff_table_diagnostics)
target_link_libraries(tst_markoff_table_diagnostics PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_table_diagnostics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_definitions(tst_markoff_table_diagnostics PRIVATE CMAKE_CURRENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(tst_markoff_table_diagnostics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_table_bugs tst_table_bugs.cpp)
add_test(NAME tst_markoff_table_bugs COMMAND tst_markoff_table_bugs)
target_link_libraries(tst_markoff_table_bugs PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_table_bugs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_bugs PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_table_integration tst_table_integration.cpp)
add_test(NAME tst_markoff_table_integration COMMAND tst_markoff_table_integration)
target_link_libraries(tst_markoff_table_integration PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_table_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_table_operations tst_table_operations.cpp)
add_test(NAME tst_markoff_table_operations COMMAND tst_markoff_table_operations)
target_link_libraries(tst_markoff_table_operations PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_table_operations PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_operations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_checkbox_toggle tst_checkbox_toggle.cpp)
add_test(NAME tst_markoff_checkbox_toggle COMMAND tst_markoff_checkbox_toggle)
target_link_libraries(tst_markoff_checkbox_toggle PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_checkbox_toggle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_checkbox_toggle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_checkbox_text_object tst_checkbox_text_object.cpp)
add_test(NAME tst_markoff_checkbox_text_object COMMAND tst_markoff_checkbox_text_object)
target_link_libraries(tst_markoff_checkbox_text_object PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_checkbox_text_object PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_checkbox_text_object PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_completion_triggers tst_completion_triggers.cpp)
add_test(NAME tst_markoff_completion_triggers COMMAND tst_markoff_completion_triggers)
target_link_libraries(tst_markoff_completion_triggers PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_completion_triggers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_completion_triggers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_scene_coordinator tst_scene_coordinator.cpp)
add_test(NAME tst_markoff_scene_coordinator COMMAND tst_markoff_scene_coordinator)
target_link_libraries(tst_markoff_scene_coordinator PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_scene_coordinator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_scene_coordinator PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_textcontrol_cursor
    tst_textcontrol_cursor.cpp
    support/textcontrol_testutil.cpp)
add_test(NAME tst_markoff_textcontrol_cursor COMMAND tst_markoff_textcontrol_cursor)
target_link_libraries(tst_markoff_textcontrol_cursor PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_textcontrol_cursor
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_cursor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_textcontrol_selection
    tst_textcontrol_selection.cpp
    support/textcontrol_testutil.cpp)
add_test(NAME tst_markoff_textcontrol_selection COMMAND tst_markoff_textcontrol_selection)
target_link_libraries(tst_markoff_textcontrol_selection PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_textcontrol_selection
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_textcontrol_editing
    tst_textcontrol_editing.cpp
    support/textcontrol_testutil.cpp)
add_test(NAME tst_markoff_textcontrol_editing COMMAND tst_markoff_textcontrol_editing)
target_link_libraries(tst_markoff_textcontrol_editing PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_textcontrol_editing
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_editing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_textcontrol_input
    tst_textcontrol_input.cpp
    support/textcontrol_testutil.cpp)
add_test(NAME tst_markoff_textcontrol_input COMMAND tst_markoff_textcontrol_input)
target_link_libraries(tst_markoff_textcontrol_input PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_textcontrol_input
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_input PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_textcontrol_links
    tst_textcontrol_links.cpp
    support/textcontrol_testutil.cpp)
add_test(NAME tst_markoff_textcontrol_links COMMAND tst_markoff_textcontrol_links)
target_link_libraries(tst_markoff_textcontrol_links PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_textcontrol_links
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_links PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_textcontrol_integration
    tst_textcontrol_integration.cpp
    support/textcontrol_testutil.cpp)
add_test(NAME tst_markoff_textcontrol_integration COMMAND tst_markoff_textcontrol_integration)
target_link_libraries(tst_markoff_textcontrol_integration PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_textcontrol_integration
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_editor_markdown_view tst_editor_markdown_view.cpp)
add_test(NAME tst_markoff_editor_markdown_view COMMAND tst_markoff_editor_markdown_view)
target_link_libraries(tst_markoff_editor_markdown_view PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_editor_markdown_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_editor_markdown_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
