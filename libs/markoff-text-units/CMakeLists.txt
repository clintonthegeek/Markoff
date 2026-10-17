cmake_minimum_required(VERSION 3.19)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

add_library(markoff_text_units STATIC
    src/TextUnits.cpp
)
add_library(Markoff::TextUnits ALIAS markoff_text_units)
set_target_properties(markoff_text_units PROPERTIES EXPORT_NAME TextUnits)
set_target_properties(markoff_text_units PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(markoff_text_units PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../markoff-core/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(markoff_text_units PUBLIC Qt6::Core)

if(DEFINED KDE_INSTALL_LIBDIR)
    install(TARGETS markoff_text_units
        EXPORT CorbomiteTargets
        ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    )
endif()
