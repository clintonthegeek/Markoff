// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/source/widget/Editor.h>

class TstSourceWidgetEditor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void editor_constructs() {
        Markoff::Source::Widget::Editor e;
        QVERIFY(e.document() == nullptr);
    }
};

QTEST_MAIN(TstSourceWidgetEditor)
#include "tst_source_widget_editor.moc"
