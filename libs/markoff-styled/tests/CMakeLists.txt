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
