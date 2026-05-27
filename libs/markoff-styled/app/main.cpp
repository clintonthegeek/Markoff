// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <markoff/styled/Editor.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    Markoff::Styled::Editor editor;
    editor.show();
    return app.exec();
}
