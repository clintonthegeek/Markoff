cmake_minimum_required(VERSION 3.19)
project(jkqtmathtext LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

# --- JKQTCommon sources (subset needed by JKQTMathText) ---
set(JKQTCOMMON_SOURCES
    jkqtcommon/jkqtpbasicimagetools.cpp
    jkqtcommon/jkqtpcachingtools.cpp
    jkqtcommon/jkqtpcodestructuring.cpp
    jkqtcommon/jkqtpconcurrencytools.cpp
    jkqtcommon/jkqtpcsstools.cpp
    jkqtcommon/jkqtpdebuggingtools.cpp
    jkqtcommon/jkqtpdrawingtools.cpp
    jkqtcommon/jkqtpenhancedpainter.cpp
    jkqtcommon/jkqtpexpected.cpp
    jkqtcommon/jkqtpgeometrytools.cpp
    jkqtcommon/jkqtphighrestimer.cpp
    jkqtcommon/jkqtpicons.cpp
    jkqtcommon/jkqtpmathtools.cpp
    jkqtcommon/jkqtpstringtools.cpp
    jkqtcommon/jkqttools.cpp
)

# --- JKQTMathText sources ---
set(JKQTMATHTEXT_SOURCES
    jkqtmathtext.cpp
    jkqtmathtexttools.cpp
    jkqtmathtextlabel.cpp
    nodes/jkqtmathtextboxinstructionnode.cpp
    nodes/jkqtmathtextbracenode.cpp
    nodes/jkqtmathtextdecoratednode.cpp
    nodes/jkqtmathtextfracnode.cpp
    nodes/jkqtmathtexthorizontallistnode.cpp
    nodes/jkqtmathtextinstructionnode.cpp
    nodes/jkqtmathtextmatrixnode.cpp
    nodes/jkqtmathtextmodifyenvironmentnode.cpp
    nodes/jkqtmathtextnode.cpp
    nodes/jkqtmathtextnodetools.cpp
    nodes/jkqtmathtextnoopnode.cpp
    nodes/jkqtmathtextsqrtnode.cpp
    nodes/jkqtmathtextsubsupernode.cpp
    nodes/jkqtmathtextsymbolnode.cpp
    nodes/jkqtmathtexttextnode.cpp
    nodes/jkqtmathtextverbatimnode.cpp
    nodes/jkqtmathtextverticallistnode.cpp
    nodes/jkqtmathtextwhitespacenode.cpp
    parsers/jkqtmathtextlatexparser.cpp
    parsers/jkqtmathtextparser.cpp
)

# --- Font resources ---
qt_add_resources(FONT_RESOURCES resources/xits.qrc resources/firamath.qrc)

# --- Static library target ---
# STATIC with hidden visibility, not a plain static library. Two Markoff
# shared libraries link this (markoff-canvas for Math blocks,
# markoff-embed-math for the display-math backend). With default visibility
# the dynamic linker merges jkqtmathtext's globals across both, so one object
# ends up with two static destructors: the second teardown freed shared state
# the first had already released (ASan: heap-use-after-free in a QHash
# destructor at process exit; it aborted the canvas render test roughly 1 run
# in 30, and ASan also reported the ODR violation). Hidden visibility gives
# each consumer a private copy, so every global has exactly one owner and one
# destructor, and the shipped artifact set is unchanged.
add_library(jkqtmathtext STATIC
    ${JKQTCOMMON_SOURCES}
    ${JKQTMATHTEXT_SOURCES}
    ${FONT_RESOURCES}
)

set_target_properties(jkqtmathtext PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

target_compile_definitions(jkqtmathtext PUBLIC
    JKQTMATHTEXT_COMPILED_WITH_XITS
    JKQTMATHTEXT_COMPILED_WITH_FIRAMATH
)

# Include paths:
#   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}       -> resolves "jkqtcommon/..."
#   PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/..     -> resolves "jkqtmathtext/..." for consumers
target_include_directories(jkqtmathtext
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jkqtmathtext
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
)

if(DEFINED KDE_INSTALL_LIBDIR)
    install(TARGETS jkqtmathtext
        EXPORT CorbomiteTargets
        RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    )
endif()
