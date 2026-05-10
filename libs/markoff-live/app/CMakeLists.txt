# STATIC library: shared between the production executable and the
# tst_live_render_qml_integration test target. Owns MainController and
# the org.markoff.live.app qml module so both consumers reach
# `loadFromModule("org.markoff.live.app", "Main")` against the
# byte-identical Main.qml. See:
#   docs/specs/2026-05-10-qml-integration-test-harness-design.md §3.2.

qt_add_library(markoff-live-app-internal STATIC
    MainController.h
    MainController.cpp
)

qt_add_qml_module(markoff-live-app-internal
    URI org.markoff.live.app
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/org/markoff/live/app"
    QML_FILES
        Main.qml
)

target_link_libraries(markoff-live-app-internal PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    markoff_live markoff_liveplugin markoff_core
)

target_include_directories(markoff-live-app-internal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

qt_add_executable(markoff-live-app
    main.cpp
)

target_link_libraries(markoff-live-app PRIVATE
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init
)

# qt_import_qml_plugins removed: a holdover from pre-Qt-6.5 patterns.
# qt_add_qml_module(STATIC ...) on Qt 6.5+ handles plugin importation
# automatically; the explicit call walked transitive deps and tried
# to resolve Qt6::<plugin> targets in this scope (where they aren't
# imported), producing ~21 spurious "link target does not exist"
# warnings per app build. See docs/2026-05-03-cmake-warnings-rationale.md.
