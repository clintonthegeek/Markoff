// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integration: a BlockKind::Table renders as a native QTextTable frame inside
// the styled Editor, surrounding text stays editable, and the model buffer is
// untouched by materialization. Drives the real Editor wiring (binding opaque
// renderer + FormatPass Table skip).
#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/OpaqueBlockRenderer.h>  // OpaqueBlockKeyProperty
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>
#include "CandidateDocumentFixture.h"

#include "support/TableTestHelpers.h"

using namespace Markoff;

static void pumpEvents() { QCoreApplication::processEvents(); }

namespace {
int frameCount(QTextDocument *doc) {
    int n = 0;
    for (QTextFrame *f : doc->rootFrame()->childFrames())
        if (qobject_cast<QTextTable *>(f)) ++n;
    return n;
}
bool hasBlockText(QTextDocument *doc, const QString &text) {
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (b.text() == text) return true;
    return false;
}
}  // namespace

class TstStyledTableRender : public QObject {
    Q_OBJECT
private slots:
    void table_renders_as_frame_in_editor() {
        auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\noutro"));
        auto& doc = *fixture_doc.document;
        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        QTextDocument *qdoc = editor.textEdit()->document();
        QCOMPARE(frameCount(qdoc), 1);

        QTextTable *t = firstTable(qdoc);
        QVERIFY(t != nullptr);
        QCOMPARE(t->rows(), 2);     // header + 1 body
        QCOMPARE(t->columns(), 2);
        QCOMPARE(t->cellAt(0, 0).firstCursorPosition().block().text(),
                 QStringLiteral("A"));
        QCOMPARE(t->cellAt(1, 1).firstCursorPosition().block().text(),
                 QStringLiteral("2"));
        // No raw pipe text leaked into the rendered document.
        QVERIFY(!qdoc->toPlainText().contains(QLatin1Char('|')));
        // Surrounding paragraphs intact.
        QVERIFY(hasBlockText(qdoc, QStringLiteral("intro")));
        QVERIFY(hasBlockText(qdoc, QStringLiteral("outro")));
    }

    // ASK-M23: the context path mapped a table-frame position through the flat
    // view, so past the first frame the reported blockKind was whatever block
    // the raw position landed on. Inside a table frame it must report the table
    // and the exact cell (the format verbs already guarded; context did not).
    void context_inside_a_table_reports_the_cell() {
        auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\noutro"));
        auto& doc = *fixture_doc.document;
        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        Markoff::EditorContext seen;
        int calls = 0;
        QObject::connect(&editor, &Markoff::MarkdownView::contextChanged, &editor,
                         [&](const Markoff::EditorContext &ctx) { seen = ctx; ++calls; });

        QTextTable *t = firstTable(editor.textEdit()->document());
        QVERIFY(t != nullptr);
        QTextCursor c = editor.textEdit()->textCursor();
        c.setPosition(t->cellAt(1, 1).firstCursorPosition().position());
        editor.textEdit()->setTextCursor(c);
        pumpEvents();

        QVERIFY(calls >= 1);
        QCOMPARE(seen.blockKind, QString::fromLatin1(Markoff::BlockKindNames::Table));
        QVERIFY(seen.inTable);
        QCOMPARE(seen.tableRow, 1);
        QCOMPARE(seen.tableCol, 1);

        // Moving back into the trustworthy prefix reports the real block again.
        QTextCursor before = editor.textEdit()->textCursor();
        before.setPosition(0);
        editor.textEdit()->setTextCursor(before);
        pumpEvents();
        QCOMPARE(seen.blockKind, QString::fromLatin1(Markoff::BlockKindNames::Paragraph));
        QVERIFY(!seen.inTable);
        QCOMPARE(seen.tableRow, -1);
        QCOMPARE(seen.tableCol, -1);
    }

    void materialization_does_not_mutate_model() {
        const QByteArray src = QByteArrayLiteral(
            "intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\noutro");
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(src);
    auto& doc = *fixture_doc.document;
        const quint64 seqBefore = doc.d2EditSequence();

        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        // Rendering the frame must not have issued any model edit.
        QCOMPARE(doc.d2EditSequence(), seqBefore);
        // The table block buffer is still the exact pipe source; save
        // round-trips it.
        const QByteArray saved = doc.serializeForSave();
        QVERIFY(saved.contains("| A | B |"));
        QVERIFY(saved.contains("|---|---|"));
    }

    // Regression (2026-05-31 SIGSEGV): a LIST after a table. FormatPass computed
    // block doc-positions from flat pipe bytes; once the table is a compact
    // frame, every following block's position overran the document → an invalid
    // QTextBlock fed to QTextList::add() crashed. A paragraph after a table
    // silently absorbed the bad position; a list dereferences it. This is the
    // production layout (docs/phase-c-status.md). Must not crash; the list must
    // render and content after the table must be intact + correctly positioned.
    void list_after_table_does_not_crash_and_renders() {
        auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n"
                             "- one\n- two\n\ntail **bold** end"));
        auto& doc = *fixture_doc.document;
        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();  // <-- crashed here before the fix

        QTextDocument *qdoc = editor.textEdit()->document();
        QCOMPARE(frameCount(qdoc), 1);
        // List items render with bullets (QTextList membership).
        bool oneInList = false, twoInList = false;
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
            if (b.text() == QStringLiteral("one")) oneInList = (b.textList() != nullptr);
            if (b.text() == QStringLiteral("two")) twoInList = (b.textList() != nullptr);
        }
        QVERIFY2(oneInList, "list item 'one' after a table not in a QTextList");
        QVERIFY2(twoInList, "list item 'two' after a table not in a QTextList");
        // Paragraph after the table is present and its inline span landed on the
        // right characters (proves correct post-frame coordinates).
        QTextBlock tail;
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next())
            if (b.text().startsWith("tail")) tail = b;
        QVERIFY(tail.isValid());
        const int boldIdx = tail.text().indexOf("bold");
        QVERIFY(boldIdx > 0);
        QTextCursor c(qdoc);
        c.setPosition(tail.position() + boldIdx + 1);
        QCOMPARE(c.charFormat().fontWeight(), int(QFont::Bold));
    }

    // The frame and model compare the complete opaque identity key, preserving
    // actor/counter identity when structural IDs reach this view.
    void frame_key_matches_block_id() {
        auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
        auto& doc = *fixture_doc.document;
        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        QTextTable *t = firstTable(editor.textEdit()->document());
        QVERIFY(t != nullptr);
        const QString key = t->frameFormat().stringProperty(OpaqueBlockKeyProperty);
        QVERIFY(!key.isEmpty());
        QString tableKey;
        for (BlockId id : doc.iterateBlocks())
            if (doc.blockKind(id) == BlockKind::Table)
                tableKey = opaqueBlockIdentityKey(id);
        QCOMPARE(key, tableKey);
    }

    // Queue #16: Editor::setFontScale forwards to StyledTableRenderer (no
    // local copy anywhere else — Editor.cpp:setFontScale calls
    // m_tableRenderer->setFontScale() directly). Prove it reaches the
    // renderer by observing its effect on materialized frame geometry
    // (cellPadding == 4.0 * fontScale, TableFrame.cpp:materializeTable)
    // rather than via StyleApplier's hash-gate (Table blocks are skipped
    // by FormatPass, so a restyle pass alone doesn't touch the frame).
    void set_font_scale_reaches_table_renderer_before_document_load() {
        auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
        auto& doc = *fixture_doc.document;
        Styled::Editor editor;
        editor.setFontScale(1.5);
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        QTextTable *t = firstTable(editor.textEdit()->document());
        QVERIFY(t != nullptr);
        QCOMPARE(t->format().cellPadding(), 4.0 * 1.5);
    }
};

QTEST_MAIN(TstStyledTableRender)
#include "tst_styled_table_render.moc"
