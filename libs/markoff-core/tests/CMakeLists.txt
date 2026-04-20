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

add_executable(tst_markoff_document tst_markoff_document.cpp)
add_test(NAME tst_markoff_document COMMAND tst_markoff_document)
target_link_libraries(tst_markoff_document PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
