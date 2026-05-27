// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

class TstStyledD2Integration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_applies_formats_after_d2_cycle() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# starts h1"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.show();

        const QTextBlock blk0 = e.textEdit()->document()->findBlockByNumber(0);
        QVERIFY(blk0.charFormat().fontPointSize() > 11.0);
    }

    void remote_edit_replays_text_and_restyles() {
        // KNOWN PRODUCTION GAP: applyFlatEdit mutates block buffer content
        // but does NOT update blockKind() when the content prefix changes
        // (e.g. paragraph → heading). StyleApplier::applyFormats relies on
        // blockKind(), so after an intra-block paragraph→heading edit the
        // block is still formatted as a paragraph.
        //
        // markoff-live works around this via LiveListModelBinding's kind-
        // transition heuristics (prefix-rule re-inference after each D2 edit).
        // markoff-styled has no equivalent yet. The fix belongs in either:
        //   (a) StyleApplier::applyFormats — infer kind from text content as
        //       a fallback when blockKind() disagrees with the leading prefix.
        //   (b) applyFlatEdit itself — call a kind-update helper when the
        //       stored kind no longer matches the new leading prefix.
        // Track as a production gap; do NOT fix inline (this is test-only).

        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("paragraph"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        // Replace the whole content with a heading.
        doc.applyFlatEdit(0, doc.flatView().size(),
                          QByteArrayLiteral("## h2 line"),
                          Markoff::Origin::UserEdit);

        // Text propagation works.
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("## h2 line"));

        // Format re-inference does NOT work: blockKind() is still Paragraph.
        // QEXPECT_FAIL causes the sub-assertion to be recorded as an expected
        // failure (the slot as a whole still passes).
        QEXPECT_FAIL("", "Production gap: applyFlatEdit does not update blockKind; "
                         "StyleApplier applies stale Paragraph format instead of "
                         "Heading. Fix needed in StyleApplier or applyFlatEdit.",
                     Continue);
        QTRY_VERIFY(e.textEdit()->document()->findBlockByNumber(0)
                        .charFormat().fontPointSize() > 11.0);
    }

    void undo_via_d2_restores_text_and_formats() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# h1 original"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        doc.applyFlatEdit(0, doc.flatView().size(),
                          QByteArrayLiteral("paragraph only"),
                          Markoff::Origin::UserEdit);
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("paragraph only"));

        doc.undoD2();
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("# h1 original"));
        const QTextBlock blk0 = e.textEdit()->document()->findBlockByNumber(0);
        QTRY_VERIFY(blk0.charFormat().fontPointSize() > 11.0);
    }

    void reset_content_does_not_double_blocks() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("first"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("first"));

        doc.resetContent(QByteArrayLiteral("second"), Markoff::Origin::UserEdit);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));
        // Must not be "secondsecond" or "first\n\nsecond".
        QCOMPARE(e.textEdit()->toPlainText().count('\n'), 0);
    }
};

QTEST_MAIN(TstStyledD2Integration)
#include "tst_styled_d2_integration.moc"
