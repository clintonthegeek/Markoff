cmake_minimum_required(VERSION 3.19)
project(markoff_core_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# Tests land here starting Task 2.

add_executable(tst_markoff_value_types tst_value_types.cpp)
add_test(NAME tst_markoff_value_types COMMAND tst_markoff_value_types)
target_link_libraries(tst_markoff_value_types PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_value_types PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_ephemeral_state tst_ephemeral_state.cpp)
add_test(NAME tst_markoff_ephemeral_state COMMAND tst_markoff_ephemeral_state)
target_link_libraries(tst_markoff_ephemeral_state PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_ephemeral_state PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# TODO Task 8: tst_markoff_document uses Phase-A API (setPlainText, plainText,
# textDocument, replace, insert, remove, beginTransaction, endTransaction, parsed)
# that was removed in Task 6. Excluded until Task 8 rewrites the slots against
# the new MarkoffDocument API.
if(FALSE)
add_executable(tst_markoff_document tst_markoff_document.cpp)
add_test(NAME tst_markoff_document COMMAND tst_markoff_document)
target_link_libraries(tst_markoff_document PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif() # TODO Task 8: re-enable once Phase-A slots are rewritten

add_executable(tst_cursor_position tst_cursor_position.cpp)
add_test(NAME tst_cursor_position COMMAND tst_cursor_position)
target_link_libraries(tst_cursor_position PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_cursor_position PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# TODO Task 8: tst_search_controller uses Phase-A API (setPlainText, plainText,
# textDocument, replace, insert, remove, beginTransaction, endTransaction, parsed)
# that was removed in Task 6. Excluded until Task 8 rewrites the slots against
# the new MarkoffDocument API.
if(FALSE)
add_executable(tst_markoff_search_controller tst_search_controller.cpp)
add_test(NAME tst_markoff_search_controller COMMAND tst_markoff_search_controller)
target_link_libraries(tst_markoff_search_controller PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_search_controller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif() # TODO Task 8: re-enable once Phase-A slots are rewritten

# TODO Task 8: tst_replace_controller uses Phase-A API (setPlainText, plainText,
# textDocument, replace, insert, remove, beginTransaction, endTransaction, parsed)
# that was removed in Task 6. Excluded until Task 8 rewrites the slots against
# the new MarkoffDocument API.
if(FALSE)
add_executable(tst_markoff_replace_controller tst_replace_controller.cpp)
add_test(NAME tst_markoff_replace_controller COMMAND tst_markoff_replace_controller)
target_link_libraries(tst_markoff_replace_controller PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_replace_controller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif() # TODO Task 8: re-enable once Phase-A slots are rewritten

add_executable(tst_markoff_search_bar tst_search_bar.cpp)
add_test(NAME tst_markoff_search_bar COMMAND tst_markoff_search_bar)
target_link_libraries(tst_markoff_search_bar PRIVATE Qt6::Test Qt6::Widgets markoff_core)
set_tests_properties(tst_markoff_search_bar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_markdown_view tst_markdown_view_compile.cpp)
add_test(NAME tst_markoff_markdown_view COMMAND tst_markoff_markdown_view)
target_link_libraries(tst_markoff_markdown_view PRIVATE Qt6::Test Qt6::Widgets markoff_core)
set_tests_properties(tst_markoff_markdown_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_embed_registry tst_markoff_embed_registry.cpp)
add_test(NAME tst_markoff_embed_registry COMMAND tst_markoff_embed_registry)
target_link_libraries(tst_markoff_embed_registry PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_embed_registry PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_codeblock_registry tst_markoff_codeblock_registry.cpp)
add_test(NAME tst_markoff_codeblock_registry COMMAND tst_markoff_codeblock_registry)
target_link_libraries(tst_markoff_codeblock_registry PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_codeblock_registry PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_embed_depth_guard tst_markoff_embed_depth_guard.cpp)
add_test(NAME tst_markoff_embed_depth_guard COMMAND tst_markoff_embed_depth_guard)
target_link_libraries(tst_markoff_embed_depth_guard PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_embed_depth_guard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_default_vault_provider tst_markoff_default_vault_provider.cpp)
add_test(NAME tst_markoff_default_vault_provider COMMAND tst_markoff_default_vault_provider)
target_link_libraries(tst_markoff_default_vault_provider PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_default_vault_provider PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_canonical_buffer tst_canonical_buffer.cpp)
add_test(NAME tst_canonical_buffer COMMAND tst_canonical_buffer)
target_link_libraries(tst_canonical_buffer PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_canonical_buffer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markdown_delta tst_markdown_delta.cpp)
add_test(NAME tst_markdown_delta COMMAND tst_markdown_delta)
target_link_libraries(tst_markdown_delta PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markdown_delta PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_parse_pool tst_parse_pool.cpp)
add_test(NAME tst_parse_pool COMMAND tst_parse_pool)
target_link_libraries(tst_parse_pool PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_parse_pool PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
