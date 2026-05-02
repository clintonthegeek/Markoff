qt_add_executable(markoff-live-render-app
    main.cpp
)

qt_add_qml_module(markoff-live-render-app
    URI org.markoff.live.render.app
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(markoff-live-render-app PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    markoff_live_render
    markoff_live_renderplugin
    markoff_foundation
)

qt_import_qml_plugins(markoff-live-render-app)
