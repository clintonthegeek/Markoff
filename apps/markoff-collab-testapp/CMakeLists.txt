find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(markoff-collab-testapp
    main.cpp
    InMemoryTransport.h
    InMemoryTransport.cpp
    CollabConsumer.h
    CollabConsumer.cpp
    EphemeralPresence.h
    EphemeralPresence.cpp
    MainWindow.h
    MainWindow.cpp
)
target_link_libraries(markoff-collab-testapp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets markoff_core markoff_canvas)
add_test(NAME markoff-collab-testapp-smoke COMMAND markoff-collab-testapp)
set_tests_properties(markoff-collab-testapp-smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS integration)

enable_testing()
add_subdirectory(tests)
