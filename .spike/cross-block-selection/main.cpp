#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    // QApplication (not QGuiApplication) so the Qt Widgets bridge is available
    // from day one — the KDAB pattern needs Widgets for native context menus.
    QApplication app(argc, argv);

    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;
    engine.loadFromModule("CrossBlockSpike", "Main");
    if (engine.rootObjects().isEmpty()) return -1;

    return app.exec();
}
