# qt_standard_project_setup establishes the Qt CMake policies used below.
if(MARKOFF_USE_COLLABTEXT_VNEXT)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test)
else()
    # Qt6::Core may already be visible because the root and Markoff libraries
    # discover shared dependencies before this directory. Still request the
    # complete legacy QML component set: importing only Core leaves QTP0005
    # and other QML policies undefined when qt_add_qml_module() runs below.
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 QuickWidgets Qml Test)
endif()
qt_standard_project_setup(REQUIRES 6.5)

if(MARKOFF_USE_COLLABTEXT_VNEXT)
    qt_add_executable(markoff-collab-testapp
        main.cpp
        InMemoryTransport.h
        InMemoryTransport.cpp
        CollabConsumer.h
        CollabConsumer.cpp
        MainWindow.h
        MainWindow.cpp
    )
    target_link_libraries(markoff-collab-testapp PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets markoff_core markoff_canvas)
    add_test(NAME markoff-collab-testapp-smoke COMMAND markoff-collab-testapp)
    set_tests_properties(markoff-collab-testapp-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
else()
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
endif()
enable_testing()
add_subdirectory(tests)
