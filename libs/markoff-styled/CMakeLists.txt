cmake_minimum_required(VERSION 3.19)
project(markoff_styled LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(markoff_styled SHARED
    include/markoff/styled/Editor.h
    include/markoff/styled/MarkoffStyledExport.h
    src/Editor.cpp
    src/StyleApplier.h
    src/StyleApplier.cpp
    src/FormatPass.h
    src/FormatPass.cpp
    src/BlockPositionWalk.h
    src/BlockPositionWalk.cpp
    src/TableFrame.h
    src/TableFrame.cpp
    src/StyledTableRenderer.h
    src/StyledTableRenderer.cpp
    include/markoff/styled/DocumentRenderer.h
    src/DocumentRenderer.cpp
    src/Detail/StyledFindAdapter.h
    src/Detail/StyledFindAdapter.cpp
    src/DocHighlighter.h
    src/DocHighlighter.cpp
    src/LinkInteraction.h
    src/LinkInteraction.cpp
    src/StructuralTextEdit.h
    src/StructuralTextEdit.cpp
)

set_target_properties(markoff_styled PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(markoff_styled
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(markoff_styled
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
            markoff_core
)

add_library(Markoff::Styled ALIAS markoff_styled)

if(DEFINED KDE_INSTALL_LIBDIR)
    install(TARGETS markoff_styled
        EXPORT CorbomiteTargets
        RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    )
endif()

if(NOT DEFINED MARKOFF_STYLED_BUILD_TESTS)
    set(MARKOFF_STYLED_BUILD_TESTS ON)
endif()
if(MARKOFF_STYLED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(app)
