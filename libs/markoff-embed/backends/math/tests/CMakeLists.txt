cmake_minimum_required(VERSION 3.19)
set_property(DIRECTORY APPEND PROPERTY LABELS unit)

find_package(Qt6 6.8 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tst_embed_math tst_embed_math.cpp)
add_test(NAME tst_embed_math COMMAND tst_embed_math)
target_link_libraries(tst_embed_math
    PRIVATE Qt6::Test Qt6::Widgets Markoff::EmbedMath Markoff::Core)
set_tests_properties(tst_embed_math PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
