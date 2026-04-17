// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Markoff Test"));
    MainWindow window;
    if (argc > 1)
        window.openFile(QString::fromLocal8Bit(argv[1]));
    window.show();
    return app.exec();
}
