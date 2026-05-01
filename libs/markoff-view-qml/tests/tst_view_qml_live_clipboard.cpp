// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QClipboard>
#include <QGuiApplication>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveClipboardController.h>
#include <markoff/view/qml/LiveListModelBinding.h>
#include <markoff/view/qml/LiveSelectionView.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

using namespace Markoff::View::Qml;
using Markoff::MarkoffDocument;
using Markoff::MarkoffEdit;

namespace {

// Seed the document, spin until the model has `expectedRows` rows.
void seedAndWait(MarkoffDocument &doc, EditorBackend &be,
                 LiveListModelBinding &binding,
                 const QByteArray &content, int expectedRows)
{
    doc.resetContent(content, Markoff::Origin::TestFixture);
    LiveBlockModel *model = binding.model();
    QVERIFY(model);
    QTRY_COMPARE(model->rowCount(), expectedRows);
    Q_UNUSED(be);
}

// Create a controller with selectionModel + blockModel wired up.
// Returns ownership via `out`.
LiveClipboardController *makeController(LiveListModelBinding &binding, QObject *parent = nullptr)
{
    auto *ctrl = new LiveClipboardController(parent);
    ctrl->setSelectionModel(binding.selectionModel());
    ctrl->setBlockModel(binding.model());
    return ctrl;
}

}  // namespace

class TstLiveClipboard : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // 1. copy() with an active selection places the selected text on the clipboard.
    void copy_single_block_selection()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        // "Hello world\n\nSecond para"
        seedAndWait(doc, be, binding, "Hello world\n\nSecond para", 2);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel);
        // Wait for block anchors.
        if (doc.blockAnchorAt(0) == std::nullopt) {
            QSignalSpy p(&doc, &MarkoffDocument::parseUpdated);
            p.wait(2000);
        }
        // Select "Hello" (bytes 0..5 in block 0).
        sel->begin(0, 0);
        sel->extend(0, 5);
        QVERIFY(sel->hasSelection());

        LiveClipboardController ctrl;
        ctrl.setSelectionModel(sel);
        ctrl.setBlockModel(binding.model());
        ctrl.copy();

        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("Hello"));
    }

    // 2. cut() copies the selection AND removes it from the document.
    void cut_removes_selection_from_document()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello world\n\nSecond para", 2);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel);
        if (doc.blockAnchorAt(0) == std::nullopt) {
            QSignalSpy p(&doc, &MarkoffDocument::parseUpdated);
            p.wait(2000);
        }

        // Select "Hello" (5 bytes).
        sel->begin(0, 0);
        sel->extend(0, 5);
        QVERIFY(sel->hasSelection());

        LiveClipboardController ctrl;
        ctrl.setSelectionModel(sel);
        ctrl.setBlockModel(binding.model());
        ctrl.cut();

        // Clipboard must contain the cut text.
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("Hello"));

        // Document must no longer start with "Hello".
        const QString docText = doc.toMarkdown();
        QVERIFY(!docText.startsWith(QStringLiteral("Hello")));
        // The remaining text should start with " world".
        QVERIFY(docText.startsWith(QStringLiteral(" world")));

        // Selection must have been cleared.
        QVERIFY(!sel->hasSelection());
    }

    // 3. paste() inserts clipboard text at degenerate cursor position.
    void paste_inserts_at_cursor()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello world\n\nSecond para", 2);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel);
        if (doc.blockAnchorAt(0) == std::nullopt) {
            QSignalSpy p(&doc, &MarkoffDocument::parseUpdated);
            p.wait(2000);
        }

        // Place a degenerate cursor at byte offset 5 ("Hello|").
        sel->begin(0, 5);
        // Degenerate: anchor == active → no selection.
        QVERIFY(!sel->hasSelection());

        QGuiApplication::clipboard()->setText(QStringLiteral("X"));

        LiveClipboardController ctrl;
        ctrl.setSelectionModel(sel);
        ctrl.setBlockModel(binding.model());
        ctrl.paste();

        // Document should now read "HelloX world…".
        const QString docText = doc.toMarkdown();
        QVERIFY(docText.startsWith(QStringLiteral("HelloX world")));
    }

    // 4. paste() replaces an active selection.
    void paste_replaces_selection()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello world\n\nSecond para", 2);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel);
        if (doc.blockAnchorAt(0) == std::nullopt) {
            QSignalSpy p(&doc, &MarkoffDocument::parseUpdated);
            p.wait(2000);
        }

        // Select "world" (bytes 6..11 = 5 chars).
        sel->begin(0, 6);
        sel->extend(0, 11);
        QVERIFY(sel->hasSelection());

        QGuiApplication::clipboard()->setText(QStringLiteral("there"));

        LiveClipboardController ctrl;
        ctrl.setSelectionModel(sel);
        ctrl.setBlockModel(binding.model());
        ctrl.paste();

        const QString docText = doc.toMarkdown();
        QVERIFY(docText.startsWith(QStringLiteral("Hello there")));

        // Selection must be cleared after paste.
        QVERIFY(!sel->hasSelection());
    }

    // 5. copy() with no selection is a no-op (clipboard unchanged).
    void copy_with_no_selection_is_noop()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello world", 1);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel);
        QVERIFY(!sel->hasSelection());

        QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));

        LiveClipboardController ctrl;
        ctrl.setSelectionModel(sel);
        ctrl.setBlockModel(binding.model());
        ctrl.copy();

        // Clipboard must be unchanged.
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("sentinel"));
    }

    // 6. cut() with no selection does not modify the document.
    void cut_with_no_selection_is_noop()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello world", 1);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel);
        QVERIFY(!sel->hasSelection());

        const QString before = doc.toMarkdown();

        LiveClipboardController ctrl;
        ctrl.setSelectionModel(sel);
        ctrl.setBlockModel(binding.model());
        ctrl.cut();

        QCOMPARE(doc.toMarkdown(), before);
    }

    // 7. copy() spanning two blocks puts text from both blocks on the clipboard,
    //    with a structural newline separator between them.
    void cross_block_copy()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        // "Hello world\n\nSecond para" → 2 blocks
        seedAndWait(doc, be, binding, "Hello world\n\nSecond para", 2);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel);
        if (doc.blockAnchorAt(0) == std::nullopt) {
            QSignalSpy p(&doc, &MarkoffDocument::parseUpdated);
            p.wait(2000);
        }

        // Select from offset 5 in block 0 ("Hello|") to offset 6 in block 1 ("Second|").
        sel->begin(0, 5);
        sel->extend(1, 6);
        QVERIFY(sel->hasSelection());

        LiveClipboardController ctrl;
        ctrl.setSelectionModel(sel);
        ctrl.setBlockModel(binding.model());
        ctrl.copy();

        const QString clip = QGuiApplication::clipboard()->text();
        QVERIFY(clip.contains(QStringLiteral("world")));
        QVERIFY(clip.contains(QStringLiteral("Second")));
        QVERIFY(clip.contains(QStringLiteral("\n")));
    }

    // 8. paste() with multi-line markdown causes the model to have more than one
    //    block after the next parse arrives.
    void paste_multiline_decomposes_on_reparse()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        // Start with a single-paragraph document.
        seedAndWait(doc, be, binding, "Hello", 1);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel);
        if (doc.blockAnchorAt(0) == std::nullopt) {
            QSignalSpy p(&doc, &MarkoffDocument::parseUpdated);
            p.wait(2000);
        }

        // Set clipboard to two-paragraph markdown.
        QGuiApplication::clipboard()->setText(QStringLiteral("foo\n\nbar"));

        // Degenerate cursor at position 0 — no selection.
        sel->begin(0, 0);
        QVERIFY(!sel->hasSelection());

        LiveClipboardController ctrl;
        ctrl.setSelectionModel(sel);
        ctrl.setBlockModel(binding.model());
        ctrl.paste();

        // After paste, "foo\n\nbar" is prepended before "Hello" → "foo\n\nbarHello"
        // which the parser splits into 2 blocks.
        QTRY_COMPARE(binding.model()->rowCount(), 2);
    }
};

QTEST_MAIN(TstLiveClipboard)
#include "tst_view_qml_live_clipboard.moc"
