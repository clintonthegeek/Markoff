cmake_minimum_required(VERSION 3.19)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)

add_executable(markoff-styled-app main.cpp)
target_link_libraries(markoff-styled-app
    PRIVATE Qt6::Widgets markoff_styled markoff_core)
