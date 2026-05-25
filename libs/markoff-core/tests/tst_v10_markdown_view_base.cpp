// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>

namespace {
class StubView : public Markoff::MarkdownView {
public:
    using Markoff::MarkdownView::MarkdownView;
};
}

class TestMarkdownViewBase : public QObject {
    Q_OBJECT
private slots:
    void defaults_are_safe() {
        StubView v;
        QCOMPARE(v.document(), nullptr);
        QCOMPARE(v.cursorPosition().line, 1);
        QCOMPARE(v.cursorPosition().column, 1);
        QCOMPARE(v.scrollPositionVisualLine(), 0.0f);
        QCOMPARE(v.isReadOnly(), false);
        QCOMPARE(v.hasCursor(), false);
        QCOMPARE(v.hasEditing(), false);
    }
    void setReadOnly_round_trips() {
        StubView v;
        v.setReadOnly(true);
        QCOMPARE(v.isReadOnly(), true);
        v.setReadOnly(false);
        QCOMPARE(v.isReadOnly(), false);
    }
    void setDocument_emits_documentChanged() {
        StubView v;
        Markoff::MarkoffDocument doc(1);
        QSignalSpy spy(&v, &Markoff::MarkdownView::documentChanged);
        v.setDocument(&doc);
        QCOMPARE(v.document(), &doc);
        QCOMPARE(spy.count(), 1);
    }
};
QTEST_MAIN(TestMarkdownViewBase)
#include "tst_v10_markdown_view_base.moc"
