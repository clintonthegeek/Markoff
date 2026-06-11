// SPDX-License-Identifier: GPL-3.0-or-later
//
// MarkdownView contract — the live leaf must retire-on-destroy (INVARIANTS #3).
//
// Seam record (INVARIANTS #1): LiveListModelBinding holds the document by a
// raw `Markoff::MarkoffDocument *` (LiveListModelBinding.cpp Private::document)
// and wires documentLoaded/d2DocumentChanged but historically never `destroyed`.
// LiveCursorState already guards EVERY document access behind
// `if (m_binding && m_binding->document())`; those guards are correct but were
// defeated because the raw pointer was never nulled when the document died.
//
// Crash repro (2026-06-10, Corbomite first-run SIGSEGV): a NoteDocument (and
// its MarkoffDocument) is freed by `Vault::unload() → teardownTree() →
// qDeleteAll(m_docs)` while a Live editor is still attached. The deferred QML
// initial-focus seed (LiveView.qml onCountChanged → requestTextCaretAtRow(0,0))
// then fires during the first window show and dereferences the freed document:
//
//     #0 Markoff::MarkoffDocument::flushPendingD2Changed  (d == junk)
//     #1 LiveListModelBinding::flushPendingDocumentChanges (if (d->document)…)
//     #2 LiveCursorState::requestTextCaretAtRow
//        … QML onCountChanged seed …
//
// INVARIANTS #5: this test drives the REAL production callsites — the exact
// Q_INVOKABLE the QML seed reaches (requestTextCaretAtRow) and the binding's
// own flush entry — not a mock. Falsifiability: without the retire-on-destroy
// connection these slots dereference freed memory and the test crashes
// (SIGSEGV), so it cannot pass vacuously.

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/EditorWidget.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveListModelBinding.h>

#include <QStringList>
#include <QTest>

using namespace Markoff::Live;

namespace {
QString makeParagraphs(int count)
{
    QStringList blocks;
    blocks.reserve(count);
    for (int i = 0; i < count; ++i)
        blocks.append(QStringLiteral("Paragraph %1 line A.").arg(i));
    return blocks.join(QStringLiteral("\n\n"));
}
} // namespace

class TestViewContractLiveDocDestroyed : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // A document destroyed while the live leaf is still attached must not
    // leave a dangling pointer: the production cursor/flush callsites must
    // become safe no-ops, and binding()->document() must read back nullptr.
    void documentDestroyed_whileAttached_retiresCleanly()
    {
        // Heap-owned so we can free it out from under the live editor,
        // exactly as Vault teardown does.
        auto *doc = new Markoff::MarkoffDocument(1);
        doc->loadFromMarkdown(makeParagraphs(8).toUtf8());

        EditorWidget w;
        w.resize(800, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        w.setDocument(doc);

        // The binding saw the document: the model populated and the QML
        // initial-focus seed established a caret.
        auto *cs = w.binding()->cursorState();
        QTRY_VERIFY2(cs->currentTextCaret().has_value(),
                     "precondition: initial-focus seed never established a "
                     "caret — the attach path did not run");
        QCOMPARE(w.binding()->document(), doc);

        // The document is freed while the editor is still alive and attached.
        delete doc;

        // Retire-on-destroy: the binding (and the base) must have dropped the
        // dangling pointer synchronously via destroyed().
        QCOMPARE(w.binding()->document(), nullptr);
        QCOMPARE(w.document(), nullptr);

        // Production callsite #1 — the exact Q_INVOKABLE the QML seed reaches.
        // Before the fix this dereferences the freed document → SIGSEGV.
        cs->requestTextCaretAtRow(0, 0);

        // Production callsite #2 — frame #1 of the crash backtrace.
        w.binding()->flushPendingDocumentChanges();

        // Survived: still detached, no crash.
        QCOMPARE(w.binding()->document(), nullptr);
    }
};

QTEST_MAIN(TestViewContractLiveDocDestroyed)
#include "tst_view_contract_live_doc_destroyed.moc"
