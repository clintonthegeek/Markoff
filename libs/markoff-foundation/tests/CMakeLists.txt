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
