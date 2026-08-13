// SPDX-License-Identifier: GPL-3.0-or-later
//
// Falsifiable test for queue #8.3 (docs/specs/2026-06-16-source-listitem-
// marker-decoration-design.md). Drives the real Markoff::Source::Editor
// widget — the production callsite — rather than calling
// MarkoffDocument::listItemDisplayMarker() directly, per INVARIANTS #5.
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QPlainTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

class TstSourceListItemMarker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void bullet_marker_painted_for_listitem_block() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- foo\n"));
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());
        QTest::qWait(50);

        // The edit-seam text is unchanged: the marker is a paint-time
        // decoration, NOT QTextDocument content. This is the contract the
        // spec pins down as "decoration, not content" — a future change
        // that silently flips to content-injection must fail this.
        QCOMPARE(e.plainTextEdit()->toPlainText(), QStringLiteral("foo"));

        // The decoration itself: block 0 is painted with "- " and has
        // left-margin space reserved for it.
        QCOMPARE(e.listItemMarkerForBlock(0), QStringLiteral("- "));
        QTextBlock b0 = e.plainTextEdit()->document()->findBlockByNumber(0);
        QVERIFY2(b0.blockFormat().leftMargin() > 0.0,
                 "ListItem block should have left margin reserved for its marker");
    }

    void non_listitem_block_has_no_marker() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());
        QTest::qWait(50);

        QCOMPARE(e.listItemMarkerForBlock(0), QString());
        QTextBlock b0 = e.plainTextEdit()->document()->findBlockByNumber(0);
        QCOMPARE(b0.blockFormat().leftMargin(), 0.0);
    }

    void ordered_marker_reflects_number() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("1. one\n2. two\n"));
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());
        QTest::qWait(50);

        QCOMPARE(e.listItemMarkerForBlock(0), QStringLiteral("1. "));
        QCOMPARE(e.listItemMarkerForBlock(1), QStringLiteral("2. "));
        // Content stays marker-free — same edit-seam contract as above.
        QCOMPARE(e.plainTextEdit()->toPlainText(), QStringLiteral("one\ntwo"));
    }
};

QTEST_MAIN(TstSourceListItemMarker)
#include "tst_source_listitem_marker.moc"
