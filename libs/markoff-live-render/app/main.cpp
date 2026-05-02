// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QQmlApplicationEngine>

/// Test app for markoff-live-render. R1C ships a window with placeholder
/// content. R2 onwards wires in EditorBackend, the LiveListModelBinding,
/// and the LiveView.qml sibling of source mode.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadFromModule("org.markoff.live.render.app", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
