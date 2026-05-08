cmake_minimum_required(VERSION 3.19)
project(markoff_collab_testapp_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 6.8 REQUIRED COMPONENTS Test Core)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

qt_add_executable(tst_d5_in_memory_transport
    tst_d5_in_memory_transport.cpp
    ../InMemoryTransport.cpp)
target_link_libraries(tst_d5_in_memory_transport PRIVATE Qt6::Test Qt6::Core markoff_core)
target_include_directories(tst_d5_in_memory_transport PRIVATE ..)
add_test(NAME tst_d5_in_memory_transport COMMAND tst_d5_in_memory_transport)

qt_add_executable(tst_d5_testapp_convergence
    tst_d5_testapp_convergence.cpp
    ../InMemoryTransport.cpp
    ../CollabConsumer.cpp)
target_link_libraries(tst_d5_testapp_convergence PRIVATE
    Qt6::Test Qt6::Core markoff_core)
target_include_directories(tst_d5_testapp_convergence PRIVATE ..)
add_test(NAME tst_d5_testapp_convergence COMMAND tst_d5_testapp_convergence)
