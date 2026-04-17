cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
add_executable(markoff-testapp
    main.cpp
    MainWindow.h
    MainWindow.cpp
)
target_link_libraries(markoff-testapp PRIVATE Qt6::Widgets markoff)

add_subdirectory(scene-demo)
