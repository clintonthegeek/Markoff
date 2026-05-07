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
    markoff_core
)

# qt_import_qml_plugins removed: a holdover from pre-Qt-6.5 patterns.
# qt_add_qml_module(STATIC ...) on Qt 6.5+ handles plugin importation
# automatically; the explicit call walked transitive deps and tried
# to resolve Qt6::<plugin> targets in this scope (where they aren't
# imported), producing ~21 spurious "link target does not exist"
# warnings per app build. See docs/2026-05-03-cmake-warnings-rationale.md.
