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
#include "CandidateDocumentFixture.h"

class TstStyledD2Integration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_applies_formats_after_d2_cycle() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("# starts h1"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.show();

        const QTextBlock blk0 = e.textEdit()->document()->findBlockByNumber(0);
        QVERIFY(blk0.charFormat().fontPointSize() > 11.0);
    }

    void remote_edit_replays_text_and_restyles() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("paragraph"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        // Replace the whole content with a heading.
        const auto block = doc.iterateBlocks().front();
        const auto oldText = doc.blockText(block);
        const auto publication = doc.applyBlockEdit({block, 0,
            static_cast<uint32_t>(oldText.size()), QByteArrayLiteral("## h2 line")});
        QVERIFY(publication.state == Markoff::VNext::ActionPublicationState::committed
                || publication.state == Markoff::VNext::ActionPublicationState::committedHistoryPending);

        // Text propagation works.
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("## h2 line"));

        // StyleApplier infers Heading from the "## " prefix and dispatches
        // Cmd::changeKind via QTimer::singleShot(0). The next d2 cycle formats
        // the block at heading size.
        QTRY_VERIFY(e.textEdit()->document()->findBlockByNumber(0)
                        .charFormat().fontPointSize() > 11.0);
    }

    void undo_via_d2_restores_text_and_formats() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("# h1 original"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        const auto block = doc.iterateBlocks().front();
        const auto oldText = doc.blockText(block);
        const auto publication = doc.applyBlockEdit({block, 0,
            static_cast<uint32_t>(oldText.size()), QByteArrayLiteral("paragraph only")});
        QVERIFY(publication.state == Markoff::VNext::ActionPublicationState::committed
                || publication.state == Markoff::VNext::ActionPublicationState::committedHistoryPending);
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
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("first"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("first"));

        // TestFixture, not UserEdit: resetContent's UserEdit branch is
        // Q_UNREACHABLE() (that origin is reserved for applyFlatEdit); a
        // full-content reset from test setup is exactly TestFixture's
        // documented purpose.
        const auto publication = doc.replaceMarkdown(QByteArrayLiteral("second"));
        QVERIFY(publication.state == Markoff::VNext::ActionPublicationState::committed
                || publication.state == Markoff::VNext::ActionPublicationState::committedHistoryPending);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));
        // Must not be "secondsecond" or "first\n\nsecond".
        QCOMPARE(e.textEdit()->toPlainText().count('\n'), 0);
    }
};

QTEST_MAIN(TstStyledD2Integration)
#include "tst_styled_d2_integration.moc"
