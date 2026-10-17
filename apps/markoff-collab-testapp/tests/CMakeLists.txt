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
set_tests_properties(tst_d5_in_memory_transport PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_vnext_action_consumer
    tst_vnext_action_consumer.cpp
    ../InMemoryTransport.cpp
    ../CollabConsumer.cpp
    ../EphemeralPresence.cpp)
target_link_libraries(tst_vnext_action_consumer PRIVATE
    Qt6::Test Qt6::Core markoff_core)
target_include_directories(tst_vnext_action_consumer PRIVATE ..)
add_test(NAME tst_vnext_action_consumer COMMAND tst_vnext_action_consumer)
set_tests_properties(tst_vnext_action_consumer PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
