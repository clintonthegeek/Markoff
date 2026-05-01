// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stage 4 / T24 — empty-paragraph hole tests.
//
// Covers the spec §3.3 lifecycle: end-of-doc Enter creates a hole,
// reification on first printable char, focus-out abandonment,
// backspace-in-empty-hole drop, Ctrl+Z drops hole + paired \n\n, and a
// mid-block-Enter regression check.
//
// Tests drive the projection layer + structural-key handler directly rather
// than through a QQuickView, mirroring the pattern in
// `tst_view_qml_live_structural_keys.cpp`. The hole-row interleaving in
// `LiveBlockModel` is observable through the model's `rowCount` / role
// queries; this is what `listView.count` is bound to in QML.

#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <Qt>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveListModelBinding.h>
#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/LiveStructuralKeyHandler.h>
#include <markoff/view/qml/ProjectionItem.h>

using Markoff::BlockAnchor;
using Markoff::MarkoffDocument;
using Markoff::MarkoffEdit;
using Markoff::View::Qml::BlockHole;
using Markoff::View::Qml::EditorBackend;
using Markoff::View::Qml::LiveBlockModel;
using Markoff::View::Qml::LiveListModelBinding;
using Markoff::View::Qml::LiveProjectionLayer;
using Markoff::View::Qml::LiveStructuralKeyHandler;

namespace {

void seedAndWait(MarkoffDocument &doc, EditorBackend &be,
                 LiveListModelBinding &binding, const QByteArray &content,
                 int expectedRows)
{
    doc.resetContent(content, Markoff::Origin::TestFixture);
    LiveBlockModel *model = binding.model();
    QVERIFY(model);
    QTRY_COMPARE(model->rowCount(), expectedRows);
    Q_UNUSED(be);
}

BlockAnchor anchorForRow(MarkoffDocument &doc, int row)
{
    const auto opt = doc.blockAnchorAt(row);
    Q_ASSERT(opt.has_value());
    return opt.value();
}

}  // namespace

class TstLiveParagraphHole : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---------------------------------------------------------------------
    // T24.1 End-of-doc Enter creates a hole; rowCount becomes 2.
    // ---------------------------------------------------------------------
    void enter_at_end_of_doc_creates_hole_and_extra_row()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello", 1);

        LiveBlockModel *model = binding.model();
        LiveProjectionLayer *layer = binding.projectionLayer();
        QVERIFY(layer);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);
        handler.setProjectionLayer(layer);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::blockHoleCreated);
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        const QString blockText = QStringLiteral("Hello");
        const bool handled = handler.tryHandle(
            Qt::Key_Return, Qt::NoModifier, anchor0, 0,
            blockText.length(), true, blockText);
        QVERIFY(handled);

        // Hole row is interleaved synchronously — no need to wait for parse.
        QCOMPARE(layer->blockHoleCount(), 1);
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(spy.count(), 1);
        const quint64 holeId = spy.first().at(0).value<quint64>();
        const int     viewRow = spy.first().at(1).toInt();
        QCOMPARE(viewRow, 1);
        QVERIFY(model->isHoleRow(viewRow));
        QCOMPARE(model->holeIdAt(viewRow), holeId);
        // The text role for the hole is empty (spec §3.3).
        const int textRole = model->roleForName("text");
        QCOMPARE(model->data(model->index(viewRow, 0), textRole).toString(),
                 QString());
        // The kind role is "paragraph".
        const int kindRole = model->roleForName("kind");
        QCOMPARE(model->data(model->index(viewRow, 0), kindRole).toString(),
                 QStringLiteral("paragraph"));
        // The doc has the paired \n\n bytes in source.
        QVERIFY(doc.toMarkdownUtf8().endsWith("\n\n"));
    }

    // ---------------------------------------------------------------------
    // T24.2 First printable character reifies; rowCount stays 2; doc has the
    // typed character.
    // ---------------------------------------------------------------------
    void first_printable_char_reifies_hole()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello", 1);

        LiveBlockModel *model = binding.model();
        LiveProjectionLayer *layer = binding.projectionLayer();

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);
        handler.setProjectionLayer(layer);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::blockHoleCreated);
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        const QString blockText = QStringLiteral("Hello");
        QVERIFY(handler.tryHandle(Qt::Key_Return, Qt::NoModifier, anchor0,
                                  0, blockText.length(), true, blockText));
        const quint64 holeId = spy.first().at(0).value<quint64>();

        // Reify: the layer drops the hole synchronously, then issues a CRDT
        // edit. The hole drops *before* applyLocalEdit returns.
        QVERIFY(layer->reifyBlockHole(holeId, QStringLiteral("X")));
        QCOMPARE(layer->blockHoleCount(), 0);

        // After parse arrives, model has 2 rows (the original "Hello" + the
        // newly-typed "X" paragraph).
        QTRY_COMPARE(model->rowCount(), 2);
        const int textRole = model->roleForName("text");
        QCOMPARE(model->data(model->index(0, 0), textRole).toString(),
                 QStringLiteral("Hello"));
        QCOMPARE(model->data(model->index(1, 0), textRole).toString(),
                 QStringLiteral("X"));
        // Doc source now contains the typed char.
        QVERIFY(doc.toMarkdownUtf8().contains("X"));
    }

    // ---------------------------------------------------------------------
    // T24.3 Focus-out abandons; rowCount returns to 1; doc still has \n\n.
    // ---------------------------------------------------------------------
    void focus_out_abandons_hole()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello", 1);

        LiveBlockModel *model = binding.model();
        LiveProjectionLayer *layer = binding.projectionLayer();

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);
        handler.setProjectionLayer(layer);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::blockHoleCreated);
        const BlockAnchor anchor0 = anchorForRow(doc, 0);
        const QString blockText = QStringLiteral("Hello");
        QVERIFY(handler.tryHandle(Qt::Key_Return, Qt::NoModifier, anchor0,
                                  0, blockText.length(), true, blockText));
        const quint64 holeId = spy.first().at(0).value<quint64>();
        QCOMPARE(model->rowCount(), 2);

        // Simulate the focus-out signal by calling the layer's drop directly
        // (this is the same path the delegate's onActiveFocusChanged invokes).
        layer->dropBlockHole(holeId);

        QCOMPARE(layer->blockHoleCount(), 0);
        QCOMPARE(model->rowCount(), 1);
        // Source rope still carries the trailing \n\n — the CRDT edit is
        // not undone by abandonment (spec §3.3).
        QVERIFY(doc.toMarkdownUtf8().endsWith("\n\n"));
    }

    // ---------------------------------------------------------------------
    // T24.4 Backspace inside empty hole at qtPos 0 drops the hole.
    // ---------------------------------------------------------------------
    void backspace_in_empty_hole_drops_it()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello", 1);

        LiveBlockModel *model = binding.model();
        LiveProjectionLayer *layer = binding.projectionLayer();

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);
        handler.setProjectionLayer(layer);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::blockHoleCreated);
        const BlockAnchor anchor0 = anchorForRow(doc, 0);
        const QString blockText = QStringLiteral("Hello");
        QVERIFY(handler.tryHandle(Qt::Key_Return, Qt::NoModifier, anchor0,
                                  0, blockText.length(), true, blockText));
        QCOMPARE(model->rowCount(), 2);
        const int viewRow = spy.first().at(1).toInt();

        // Backspace at qtPos 0 in the empty hole row.
        QVERIFY(handler.tryHandleHoleBackspace(viewRow, 0, QString()));
        QCOMPARE(layer->blockHoleCount(), 0);
        QCOMPARE(model->rowCount(), 1);
    }

    // ---------------------------------------------------------------------
    // T24.5 Ctrl+Z inside an unreified hole drops the hole and the paired
    // `\n\n` edit; doc returns to pre-Enter state.
    // ---------------------------------------------------------------------
    void undo_drops_hole_and_paired_edit()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello", 1);

        LiveBlockModel *model = binding.model();
        LiveProjectionLayer *layer = binding.projectionLayer();

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);
        handler.setProjectionLayer(layer);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::blockHoleCreated);
        const BlockAnchor anchor0 = anchorForRow(doc, 0);
        const QString blockText = QStringLiteral("Hello");
        QVERIFY(handler.tryHandle(Qt::Key_Return, Qt::NoModifier, anchor0,
                                  0, blockText.length(), true, blockText));
        QCOMPARE(model->rowCount(), 2);
        QVERIFY(doc.toMarkdownUtf8().endsWith("\n\n"));

        // Spec §6 case 1: Ctrl+Z drops the hole AND the paired edit.
        layer->undoWithHoles();

        QCOMPARE(layer->blockHoleCount(), 0);
        QTRY_COMPARE(model->rowCount(), 1);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("Hello"));
    }

    // ---------------------------------------------------------------------
    // T24.6 Mid-block Enter still works (regression check).
    // ---------------------------------------------------------------------
    void mid_block_enter_regression()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "HelloWorld", 1);

        LiveBlockModel *model = binding.model();
        LiveProjectionLayer *layer = binding.projectionLayer();

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);
        handler.setProjectionLayer(layer);

        const BlockAnchor anchor0 = anchorForRow(doc, 0);
        const QString blockText = QStringLiteral("HelloWorld");
        // Cursor between "Hello" and "World" — split at qtPos 5.
        QVERIFY(handler.tryHandle(Qt::Key_Return, Qt::NoModifier, anchor0,
                                  0, 5, true, blockText));

        // Mid-block split produces two real blocks; no hole created.
        QCOMPARE(layer->blockHoleCount(), 0);
        QTRY_COMPARE(model->rowCount(), 2);
        const int textRole = model->roleForName("text");
        QCOMPARE(model->data(model->index(0, 0), textRole).toString(),
                 QStringLiteral("Hello"));
        QCOMPARE(model->data(model->index(1, 0), textRole).toString(),
                 QStringLiteral("World"));
    }

    // ---------------------------------------------------------------------
    // Spec §9: multiple stacked Enter presses at EOB → second is no-op.
    // ---------------------------------------------------------------------
    void second_enter_at_eob_is_noop()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "Hello", 1);

        LiveBlockModel *model = binding.model();
        LiveProjectionLayer *layer = binding.projectionLayer();

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);
        handler.setProjectionLayer(layer);

        const BlockAnchor anchor0 = anchorForRow(doc, 0);
        const QString blockText = QStringLiteral("Hello");
        QVERIFY(handler.tryHandle(Qt::Key_Return, Qt::NoModifier, anchor0,
                                  0, blockText.length(), true, blockText));
        QCOMPARE(layer->blockHoleCount(), 1);
        QCOMPARE(model->rowCount(), 2);
        // First Enter appends exactly one `\n\n`.
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("Hello\n\n"));

        // Second Enter at EOB — the structural-key handler now detects that a
        // hole already exists for this parsed row and short-circuits without
        // issuing a duplicate `\n\n` source edit (spec §9: "second Enter is a
        // no-op; the hole is already there and empty").
        QVERIFY(handler.tryHandle(Qt::Key_Return, Qt::NoModifier, anchor0,
                                  0, blockText.length(), true, blockText));
        QCOMPARE(layer->blockHoleCount(), 1);
        QCOMPARE(model->rowCount(), 2);
        // Source bytes did NOT accumulate.
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("Hello\n\n"));
    }
};

QTEST_MAIN(TstLiveParagraphHole)
#include "tst_view_qml_live_paragraph_hole.moc"
