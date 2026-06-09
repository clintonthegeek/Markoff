// SPDX-License-Identifier: GPL-3.0-or-later
//
// Find highlights in the styled view must be frame-aware: a match
// AFTER a rendered QTextTable lands at the visible-text position, not
// at flat-byte arithmetic positions (spec §6; the 2026-05-31 SIGSEGV
// class — positions must never be derived from flat pipe-source bytes).
// Matches INSIDE a table frame are a documented degradation: counted by
// the controller, no highlight rendered, but navigation scrolls to the
// frame (caret parked at its first position).
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

#include "support/TableTestHelpers.h"

using Markoff::FindController;

class TstStyledFindAdapter : public QObject {
    Q_OBJECT

    static QList<int> expectedPositions(const QString &plain, const QString &needle) {
        QList<int> out;
        for (int from = 0;;) {
            const int hit = plain.indexOf(needle, from);
            if (hit < 0) break;
            out.append(hit);
            from = hit + 1;
        }
        return out;
    }

private Q_SLOTS:
    void highlights_align_without_tables() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // First block carries a non-ASCII char so UTF-8 byte offsets and
        // QChar positions diverge ahead of the first match.
        doc.loadFromMarkdown(QByteArray("caf\xC3\xA9 target alpha\n\nbravo target middle"));
        e.setDocument(&doc);
        QTest::qWait(50);

        FindController fc(&doc);
        fc.activate();
        fc.setNeedle(QStringLiteral("target"));
        QCOMPARE(fc.matchCount(), 2);
        e.attachFindController(&fc);

        QTextEdit *te = e.textEdit();
        QVERIFY(te);
        const QString plain = te->toPlainText();
        const QList<int> expected = expectedPositions(plain, QStringLiteral("target"));
        QCOMPARE(expected.size(), 2);
        const auto sels = te->extraSelections();
        QCOMPARE(sels.size(), 2);
        QCOMPARE(sels[0].cursor.selectionStart(), expected[0]);
        QCOMPARE(sels[1].cursor.selectionStart(), expected[1]);
        for (const auto &s : sels)
            QCOMPARE(s.cursor.selectedText(), QStringLiteral("target"));
    }

    void match_after_table_lands_at_visible_position() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray(
            "| A | B |\n| --- | --- |\n| x | y |\n\nafter target end"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        QTest::qWait(50);

        FindController fc(&doc);
        fc.activate();
        fc.setNeedle(QStringLiteral("target"));
        e.attachFindController(&fc);

        QTextEdit *te = e.textEdit();
        QVERIFY(te);
        const auto sels = te->extraSelections();
        QCOMPARE(sels.size(), 1);
        QTextCursor cur = sels[0].cursor;
        QCOMPARE(cur.selectedText(), QStringLiteral("target"));
        // Selects the actual glyphs in the visible document:
        const QTextBlock blk = cur.block();
        QCOMPARE(blk.text().mid(cur.selectionStart() - blk.position(), 6),
                 QStringLiteral("target"));
    }

    void match_inside_table_is_skipped_without_crash() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray(
            "| A | B |\n| --- | --- |\n| target | y |"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        QTest::qWait(50);

        FindController fc(&doc);
        fc.activate();
        fc.setNeedle(QStringLiteral("target"));
        QCOMPARE(fc.matchCount(), 1);  // controller still counts it
        e.attachFindController(&fc);

        QTextEdit *te = e.textEdit();
        QVERIFY(te);
        // Documented degradation: no highlight for matches inside a frame —
        // the raw pipe-source offsets do not correspond to doc positions.
        QCOMPARE(te->extraSelections().size(), 0);
        // Navigation onto the frame match scrolls to the frame (spec §6):
        // the caret parks at the frame's first position rather than the
        // adapter going inert.
        fc.findNext();
        QTextTable *table = firstTable(te->document());
        QVERIFY(table);
        const int caret = te->textCursor().position();
        QVERIFY2(caret >= table->firstPosition() && caret <= table->lastPosition(),
                 qPrintable(QStringLiteral("caret %1 not within frame [%2, %3]")
                                .arg(caret)
                                .arg(table->firstPosition())
                                .arg(table->lastPosition())));
    }

    void match_in_multiline_code_block_lands_at_visible_position() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // CodeBlock buffers keep internal '\n' — the model block spans
        // multiple top-level QTextBlocks; the match is on the second line.
        doc.loadFromMarkdown(QByteArray(
            "intro\n\n```\nline one\nfoo target bar\n```\n\ntail"));
        e.setDocument(&doc);
        QTest::qWait(50);

        FindController fc(&doc);
        fc.activate();
        fc.setNeedle(QStringLiteral("target"));
        e.attachFindController(&fc);

        QTextEdit *te = e.textEdit();
        QVERIFY(te);
        const QString plain = te->toPlainText();
        const auto sels = te->extraSelections();
        QCOMPARE(sels.size(), 1);
        QCOMPARE(sels[0].cursor.selectionStart(),
                 int(plain.indexOf(QStringLiteral("target"))));
        QCOMPARE(sels[0].cursor.selectedText(), QStringLiteral("target"));
    }

    void navigation_places_caret_without_focus_steal() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("first target\n\nsecond target"));
        e.setDocument(&doc);
        QTest::qWait(50);

        FindController fc(&doc);
        fc.activate();
        fc.setNeedle(QStringLiteral("target"));
        e.attachFindController(&fc);

        QTextEdit *te = e.textEdit();
        QVERIFY(te);
        const QString plain = te->toPlainText();
        const QList<int> expected = expectedPositions(plain, QStringLiteral("target"));
        QCOMPARE(expected.size(), 2);
        fc.findNext();
        const int idx = fc.currentMatchIndex();
        QVERIFY(idx >= 0);
        // Caret parked on the current match's position in visible text:
        QVERIFY(expected.contains(te->textCursor().position()));
    }

    void detach_clears_highlights() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("one target"));
        e.setDocument(&doc);
        QTest::qWait(50);

        FindController fc(&doc);
        fc.activate();
        fc.setNeedle(QStringLiteral("target"));
        e.attachFindController(&fc);
        QTextEdit *te = e.textEdit();
        QVERIFY(te);
        QCOMPARE(te->extraSelections().size(), 1);
        e.detachFindController();
        QCOMPARE(te->extraSelections().size(), 0);
    }
};

QTEST_MAIN(TstStyledFindAdapter)
#include "tst_styled_find_adapter.moc"
