# qt_standard_project_setup establishes Qt CMake policies (QTP0004, QTP0005, etc.)
# required by qt_add_qml_module. markoff-live calls this in its own scope via a
# separate project() call; we mirror that here so this subdirectory gets the same
# policies. find_package is guarded since libs/markoff-live already ran it.
if(NOT TARGET Qt6::Core)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 QuickWidgets Qml Test)
endif()
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(markoff-collab-testapp
    main.cpp
    InMemoryTransport.h
    InMemoryTransport.cpp
    CollabConsumer.h
    CollabConsumer.cpp
    MainWindow.h
    MainWindow.cpp
)
qt_add_qml_module(markoff-collab-testapp
    URI org.markoff.collab.app
    VERSION 1.0
    QML_FILES
        CollabMain.qml
)
target_link_libraries(markoff-collab-testapp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::QuickWidgets Qt6::Qml
    markoff_core
    markoff_live
    markoff_liveplugin
)
enable_testing()
add_subdirectory(tests)
