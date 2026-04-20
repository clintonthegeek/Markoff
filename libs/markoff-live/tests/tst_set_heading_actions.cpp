// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QTest>
#include <QAction>

#include <markoff/Editor.h>

using namespace Markoff;

class TstSetHeading : public QObject {
    Q_OBJECT

private:
    Editor *makeEditor(const QString &text) {
        auto *ed = new Editor;
        ed->resize(400, 300);
        ed->setPlainText(text);
        ed->show();
        (void)QTest::qWaitForWindowExposed(ed);
        const auto items = ed->scene()->items();
        if (!items.isEmpty())
            items.first()->setFocus();
        ed->selectAll();
        return ed;
    }

private slots:
    void registersAllSixActions() {
        Editor ed;
        QVERIFY(ed.action(ActionId::SetHeading1));
        QVERIFY(ed.action(ActionId::SetHeading2));
        QVERIFY(ed.action(ActionId::SetHeading3));
        QVERIFY(ed.action(ActionId::SetHeading4));
        QVERIFY(ed.action(ActionId::SetHeading5));
        QVERIFY(ed.action(ActionId::SetHeading6));
    }

    void shortcutsAreCtrlDigit() {
        Editor ed;
        QCOMPARE(ed.action(ActionId::SetHeading1)->shortcut(),
                 QKeySequence(Qt::CTRL | Qt::Key_1));
        QCOMPARE(ed.action(ActionId::SetHeading6)->shortcut(),
                 QKeySequence(Qt::CTRL | Qt::Key_6));
    }

    void appliesEachLevel_data() {
        QTest::addColumn<int>("level");
        QTest::addColumn<QString>("expected");
        QTest::newRow("H1") << 1 << QStringLiteral("# example");
        QTest::newRow("H2") << 2 << QStringLiteral("## example");
        QTest::newRow("H3") << 3 << QStringLiteral("### example");
        QTest::newRow("H4") << 4 << QStringLiteral("#### example");
        QTest::newRow("H5") << 5 << QStringLiteral("##### example");
        QTest::newRow("H6") << 6 << QStringLiteral("###### example");
    }
    void appliesEachLevel() {
        QFETCH(int, level);
        QFETCH(QString, expected);
        const ActionId ids[6] = {
            ActionId::SetHeading1, ActionId::SetHeading2, ActionId::SetHeading3,
            ActionId::SetHeading4, ActionId::SetHeading5, ActionId::SetHeading6,
        };
        auto *ed = makeEditor(QStringLiteral("example"));
        ed->action(ids[level - 1])->trigger();
        QCOMPARE(ed->toPlainText(), expected);
        delete ed;
    }

    void replacesExistingLevel() {
        auto *ed = makeEditor(QStringLiteral("### example"));
        ed->action(ActionId::SetHeading1)->trigger();
        QCOMPARE(ed->toPlainText(), QStringLiteral("# example"));
        delete ed;
    }
};

QTEST_MAIN(TstSetHeading)
#include "tst_set_heading_actions.moc"
