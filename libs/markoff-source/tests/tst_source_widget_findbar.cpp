// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QLineEdit>
#include <QTest>

#include <markoff/source/Editor.h>
#include <markoff/source/FindBar.h>
#include <markoff/core/MarkoffDocument.h>

class TstSourceWidgetFindBar : public QObject {
    Q_OBJECT
private:
    Markoff::MarkoffDocument *makeDoc(const QByteArray &seed) {
        auto *d = new Markoff::MarkoffDocument(1);
        // D2: loadFromMarkdown populates the per-block structure that
        // the binding and find engine read from.
        d->loadFromMarkdown(seed);
        return d;
    }

private Q_SLOTS:
    void findbar_finds_first_match() {
        Markoff::Source::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("the quick brown fox quick"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("quick"));
        QTRY_VERIFY(!e.plainTextEdit()->extraSelections().isEmpty());
        QCOMPARE(e.plainTextEdit()->extraSelections().size(), 2);
        delete doc;
    }

    void findbar_next_prev_navigation() {
        Markoff::Source::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("aaa bbb aaa ccc aaa"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("aaa"));
        QTRY_COMPARE(e.plainTextEdit()->extraSelections().size(), 3);
        // Cursor sits on first match initially.
        const int firstPos = e.plainTextEdit()->textCursor().position();
        QMetaObject::invokeMethod(&bar, "next");
        QVERIFY(e.plainTextEdit()->textCursor().position() != firstPos);
        QMetaObject::invokeMethod(&bar, "prev");
        QCOMPARE(e.plainTextEdit()->textCursor().position(), firstPos);
        delete doc;
    }

    void findbar_close_clears_highlights() {
        Markoff::Source::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("hello hello"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("hello"));
        QTRY_VERIFY(!e.plainTextEdit()->extraSelections().isEmpty());
        bar.deactivate();
        QVERIFY(e.plainTextEdit()->extraSelections().isEmpty());
        delete doc;
    }
};

QTEST_MAIN(TstSourceWidgetFindBar)
#include "tst_source_widget_findbar.moc"
