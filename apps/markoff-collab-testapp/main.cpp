// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen")) {
        app.processEvents();
        return 0;
    }
    return app.exec();
}
