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
#include "CandidateDocumentFixture.h"

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
        auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArray("caf\xC3\xA9 target alpha\n\nbravo target middle"));
        auto& doc = *fixture_doc.document;
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

    // ASK-M25: setDocument() re-wires the binding, style applier, link
    // interaction and context hooks but never re-pointed the find adapter:
    // after a swap the adapter kept listening to the retired document, so
    // highlights stopped refreshing for the live one.
    void find_adapter_follows_a_document_swap() {
        Markoff::Styled::Editor e;
        // The match lives in the SECOND block, so its BlockId is not one the
        // next document mints.
        auto first = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("first block\n\nsecond target here"));
        e.setDocument(first.document.get());
        QTest::qWait(50);

        FindController fc(first.document.get());
        fc.activate();
        fc.setNeedle(QStringLiteral("target"));
        e.attachFindController(&fc);
        QCOMPARE(e.textEdit()->extraSelections().size(), 1);
        QCOMPARE(e.textEdit()->extraSelections().first().cursor.selectedText(),
                 QStringLiteral("target"));

        // A single-block document: the match's block does not exist here, so
        // the honest render is no highlight — and it must happen at swap time.
        // With the adapter still bound to the retired document, nothing
        // re-renders and the stale selection survives.
        auto second = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("a completely different body"));
        e.setDocument(second.document.get());
        QTest::qWait(50);

        // The match belongs to the retired document, whose block is not in
        // this one: the adapter must re-derive at swap time and drop the
        // highlight. Bound to the old document it never re-renders, and the
        // stale selection survives.
        QVERIFY(e.textEdit()->extraSelections().isEmpty());
        e.detachFindController();
    }

    void match_after_table_lands_at_visible_position() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArray(
            "| A | B |\n| --- | --- |\n| x | y |\n\nafter target end"));
    auto& doc = *fixture_doc.document;
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
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArray(
            "| A | B |\n| --- | --- |\n| target | y |"));
    auto& doc = *fixture_doc.document;
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
        auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArray(
            "intro\n\n```\nline one\nfoo target bar\n```\n\ntail"));
        auto& doc = *fixture_doc.document;
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
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArray("first target\n\nsecond target"));
    auto& doc = *fixture_doc.document;
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
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArray("one target"));
    auto& doc = *fixture_doc.document;
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
