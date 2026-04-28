cmake_minimum_required(VERSION 3.19)
project(markoff_foundation_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Test executables are added incrementally as tasks land.
# Convention:
#   add_executable(tst_FOO tst_foo.cpp)
#   add_test(NAME tst_FOO COMMAND tst_FOO)
#   target_link_libraries(tst_FOO PRIVATE Qt6::Test markoff_foundation)
#   set_tests_properties(tst_FOO PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_markoff_edit tst_markoff_edit.cpp)
add_test(NAME tst_markoff_edit COMMAND tst_markoff_edit)
target_link_libraries(tst_markoff_edit PRIVATE Qt6::Test markoff_foundation)
set_tests_properties(tst_markoff_edit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_anchor_json tst_anchor_json.cpp)
add_test(NAME tst_anchor_json COMMAND tst_anchor_json)
target_link_libraries(tst_anchor_json PRIVATE Qt6::Test markoff_foundation)
set_tests_properties(tst_anchor_json PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_selection tst_selection.cpp)
add_test(NAME tst_selection COMMAND tst_selection)
target_link_libraries(tst_selection PRIVATE Qt6::Test markoff_foundation)
set_tests_properties(tst_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_fold_ref tst_fold_ref.cpp)
add_test(NAME tst_fold_ref COMMAND tst_fold_ref)
target_link_libraries(tst_fold_ref PRIVATE Qt6::Test markoff_foundation)
set_tests_properties(tst_fold_ref PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Note: target name differentiated from markoff-core's tst_markoff_document
# (CMake target names must be globally unique within a single build tree).
add_executable(tst_foundation_markoff_document tst_markoff_document.cpp)
add_test(NAME tst_foundation_markoff_document COMMAND tst_foundation_markoff_document)
target_link_libraries(tst_foundation_markoff_document PRIVATE Qt6::Test markoff_foundation)
set_tests_properties(tst_foundation_markoff_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_foundation_session tst_foundation_session.cpp)
add_test(NAME tst_foundation_session COMMAND tst_foundation_session)
target_link_libraries(tst_foundation_session PRIVATE Qt6::Test markoff_foundation)
set_tests_properties(tst_foundation_session PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_foundation_parse_pool tst_foundation_parse_pool.cpp)
add_test(NAME tst_foundation_parse_pool COMMAND tst_foundation_parse_pool)
target_link_libraries(tst_foundation_parse_pool PRIVATE Qt6::Test markoff_foundation)
set_tests_properties(tst_foundation_parse_pool PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
