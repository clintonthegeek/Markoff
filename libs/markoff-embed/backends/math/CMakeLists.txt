cmake_minimum_required(VERSION 3.19)
project(markoff_embed_math LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

add_library(markoff_embed_math SHARED
    include/markoff/embed/math/MathBackend.h
    src/MathBackend.cpp
)
add_library(Markoff::EmbedMath ALIAS markoff_embed_math)
set_target_properties(markoff_embed_math PROPERTIES EXPORT_NAME EmbedMath)

target_include_directories(markoff_embed_math PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../markoff-core/include>
)
target_link_libraries(markoff_embed_math
    PUBLIC Markoff::Core
    PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets jkqtmathtext
)

if(DEFINED KDE_INSTALL_LIBDIR)
    include(KDEInstallDirs)
    install(TARGETS markoff_embed_math
        EXPORT CorbomiteTargets
        LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/markoff
        DESTINATION ${KDE_INSTALL_INCLUDEDIR}
    )
endif()

if(NOT DEFINED MARKOFF_EMBED_MATH_BUILD_TESTS)
    if(DEFINED MARKOFF_BUILD_TESTS)
        set(MARKOFF_EMBED_MATH_BUILD_TESTS ${MARKOFF_BUILD_TESTS})
    else()
        set(MARKOFF_EMBED_MATH_BUILD_TESTS ON)
    endif()
endif()
if(MARKOFF_EMBED_MATH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
