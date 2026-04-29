qt_add_executable(markoff-view-qml-app
    main.cpp
)

qt_add_qml_module(markoff-view-qml-app
    URI org.markoff.view.qml.app
    VERSION 1.0
    QML_FILES
        Main.qml
        AstInspectorPane.qml
)

target_link_libraries(markoff-view-qml-app PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    markoff_view_qml
    markoff_foundation
)

qt_import_qml_plugins(markoff-view-qml-app)
