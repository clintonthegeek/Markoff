// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QLineEdit>
#include <QTest>

#include <markoff/source/widget/Editor.h>
#include <markoff/source/widget/FindBar.h>
#include <markoff-foundation/MarkoffDocument.h>

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
        Markoff::Source::Widget::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("the quick brown fox quick"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::Widget::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("quick"));
        QTRY_VERIFY(!e.extraSelections().isEmpty());
        QCOMPARE(e.extraSelections().size(), 2);
        delete doc;
    }

    void findbar_next_prev_navigation() {
        Markoff::Source::Widget::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("aaa bbb aaa ccc aaa"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::Widget::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("aaa"));
        QTRY_COMPARE(e.extraSelections().size(), 3);
        // Cursor sits on first match initially.
        const int firstPos = e.textCursor().position();
        QMetaObject::invokeMethod(&bar, "next");
        QVERIFY(e.textCursor().position() != firstPos);
        QMetaObject::invokeMethod(&bar, "prev");
        QCOMPARE(e.textCursor().position(), firstPos);
        delete doc;
    }

    void findbar_close_clears_highlights() {
        Markoff::Source::Widget::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("hello hello"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::Widget::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("hello"));
        QTRY_VERIFY(!e.extraSelections().isEmpty());
        bar.deactivate();
        QVERIFY(e.extraSelections().isEmpty());
        delete doc;
    }
};

QTEST_MAIN(TstSourceWidgetFindBar)
#include "tst_source_widget_findbar.moc"
