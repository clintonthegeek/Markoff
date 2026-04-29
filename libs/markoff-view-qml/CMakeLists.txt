find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}" "${ECM_KDE_MODULE_DIR}")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Test)
find_package(KF6SyntaxHighlighting REQUIRED)

qt_add_library(markoff_view_qml STATIC)

target_include_directories(markoff_view_qml PUBLIC include/)

target_link_libraries(markoff_view_qml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        markoff_foundation
)
