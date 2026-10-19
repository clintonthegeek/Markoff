cmake_minimum_required(VERSION 3.19)
project(markoff_embed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Svg)

add_library(markoff_embed SHARED
    include/markoff/embed/EmbedRuntime.h
    src/EmbedRuntime.cpp
)
add_library(Markoff::Embed ALIAS markoff_embed)
set_target_properties(markoff_embed PROPERTIES EXPORT_NAME Embed)

target_include_directories(markoff_embed PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../markoff-core/include>
)
target_link_libraries(markoff_embed
    PUBLIC Markoff::Core
    PRIVATE Qt6::Core Qt6::Gui Qt6::Svg
)

# Export gated exactly like the other leaves: only inside a host that defines
# KDE_INSTALL_LIBDIR (Corbomite). Standalone Markoff installs nothing.
if(DEFINED KDE_INSTALL_LIBDIR)
    include(KDEInstallDirs)
    install(TARGETS markoff_embed
        EXPORT CorbomiteTargets
        LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/markoff
        DESTINATION ${KDE_INSTALL_INCLUDEDIR}
    )
endif()

# Follows the family switch, which the Markoff root sets before adding this
# leaf; a leaf configured on its own defaults to building its tests.
if(NOT DEFINED MARKOFF_EMBED_BUILD_TESTS)
    if(DEFINED MARKOFF_BUILD_TESTS)
        set(MARKOFF_EMBED_BUILD_TESTS ${MARKOFF_BUILD_TESTS})
    else()
        set(MARKOFF_EMBED_BUILD_TESTS ON)
    endif()
endif()
if(MARKOFF_EMBED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
