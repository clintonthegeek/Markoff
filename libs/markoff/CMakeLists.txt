cmake_minimum_required(VERSION 3.19)
project(markoff VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

if(NOT TARGET jkqtmathtext)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../jkqtmathtext ${CMAKE_CURRENT_BINARY_DIR}/jkqtmathtext)
endif()

# Markoff editor library
add_library(markoff STATIC
    include/markoff/Editor.h
    include/markoff/FoldingTypes.h
    include/markoff/LinkRenderer.h
    include/markoff/SearchBar.h
    src/SelectableItem.h
    src/BlockItem.h
    src/BlockItem.cpp
    src/StubBlockItem.h
    src/StubBlockItem.cpp
    src/ImageBlockItem.h
    src/ImageBlockItem.cpp
    src/MarkdownTextItem.h
    src/MarkdownTextItem.cpp
    src/MathRenderer.h
    src/MathRenderer.cpp
    src/MathTextObject.h
    src/MathTextObject.cpp
    src/CheckboxTextObject.h
    src/CheckboxTextObject.cpp
    src/SelectionManager.h
    src/SelectionManager.cpp
    src/SelectionScene.h
    src/SelectionScene.cpp
    src/SceneCoordinator.h
    src/SceneCoordinator.cpp
    src/Theme.cpp
    src/TextControl.cpp
    src/Editor.cpp
    src/LinkRenderer.cpp
    src/SearchBar.cpp
    src/MarkdownHighlighter.cpp
    src/DecoratedRange.cpp
    src/FoldingTypes.cpp
    src/FoldingModel.cpp
    src/FoldArrowColumn.cpp
    src/FoldGutter.cpp
    src/ResourceProvider.cpp
    src/TableSerializer.h
    src/TableSerializer.cpp
    src/TableConverter.h
    src/TableConverter.cpp
    src/TableStyle.h
)
set_target_properties(markoff PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Markoff::Markoff ALIAS markoff)

target_include_directories(markoff
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(markoff
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        MarkoffParser::MarkoffParser
    PRIVATE
        KF6::SyntaxHighlighting
        jkqtmathtext
)

enable_testing()
add_subdirectory(tests)
add_subdirectory(app)
