// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression coverage for the find-highlight coordinate space.
//
// The source widget's QTextDocument is seeded from widgetFlatView(), which
// joins blocks with a single '\n'. SourceFindAdapter::globalCharPosFor()
// historically advanced by 2 chars per block boundary (the canonical
// "\n\n" interBlockSeparator), drifting every match after the first block
// by +1 char per preceding boundary — the same bug class as the
// setHeadingLevel SEP_LEN drift fixed in a0d8f5b (docs/queue.md
// Discipline Log). Proven failing against the += 2 implementation.

#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

using Markoff::FindController;

class TstSourceFindAdapter : public QObject {
    Q_OBJECT

    // Three paragraphs; the needle appears once per block. The first block
    // carries a non-ASCII char so UTF-8 byte offsets and QChar positions
    // diverge ahead of the first match.
    static QByteArray fixture() {
        return QByteArray("caf\xC3\xA9 target alpha\n\n"
                          "bravo target middle\n\n"
                          "charlie end target");
    }

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
    void highlights_align_with_visible_text_across_blocks() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(fixture());
        e.setDocument(&doc);
        QTest::qWait(50);

        const QString plain = e.toPlainText();
        const QString needle = QStringLiteral("target");
        const QList<int> expected = expectedPositions(plain, needle);
        QCOMPARE(expected.size(), 3);  // fixture sanity: one match per block

        FindController fc(&doc);
        fc.activate();
        fc.setNeedle(needle);
        QCOMPARE(fc.matchCount(), 3);

        e.attachFindController(&fc);
        const auto sels = e.extraSelections();
        QCOMPARE(sels.size(), 3);
        for (int i = 0; i < sels.size(); ++i) {
            QCOMPARE(sels[i].cursor.selectionStart(), expected[i]);
            QCOMPARE(sels[i].cursor.selectionEnd(),
                     expected[i] + needle.size());
        }
    }

    void navigation_places_caret_at_visible_match() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(fixture());
        e.setDocument(&doc);
        QTest::qWait(50);

        const QString plain = e.toPlainText();
        const QString needle = QStringLiteral("target");
        const QList<int> expected = expectedPositions(plain, needle);
        QCOMPARE(expected.size(), 3);

        FindController fc(&doc);
        fc.activate();
        fc.setNeedle(needle);
        e.attachFindController(&fc);

        // Whatever match index navigation selects, the caret must land on
        // that match's position in the *visible* text.
        for (int i = 0; i < 3; ++i) {
            fc.findNext();
            const int idx = fc.currentMatchIndex();
            QVERIFY(idx >= 0 && idx < expected.size());
            QCOMPARE(e.textCursor().position(), expected[idx]);
        }
    }
};

QTEST_MAIN(TstSourceFindAdapter)
#include "tst_source_find_adapter.moc"
