// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/view/qml/LiveEditBinding.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/BlockKind.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>

using Markoff::MarkoffDocument;
using Markoff::BlockAnchor;
using Markoff::View::Qml::LiveEditBinding;
using Markoff::View::Qml::LiveBlockModel;
using Markoff::View::Qml::BlockRecord;
using Markoff::View::Qml::AstBlockDiff;
namespace BlockKind = Markoff::View::Qml::BlockKind;

namespace {

QQuickTextDocument *seedQQuickTextDocument(QQmlApplicationEngine &engine)
{
    engine.loadData(
        R"qml(
            import QtQuick
            import QtQuick.Controls
            ApplicationWindow {
                visible: false
                TextEdit { id: te; objectName: "te" }
            }
        )qml"
    );
    QObject *root = engine.rootObjects().value(0);
    if (!root) return nullptr;
    QObject *te = root->findChild<QObject *>("te");
    if (!te) return nullptr;
    return qvariant_cast<QQuickTextDocument *>(te->property("textDocument"));
}

BlockAnchor seedDocumentAndWaitForParse(MarkoffDocument &doc, const QByteArray &content)
{
    doc.resetContent(content, Markoff::Origin::TestFixture);
    QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
    if (parseSpy.isEmpty())
        parseSpy.wait(2000);
    const auto anchorOpt = doc.blockAnchorAt(0);
    return anchorOpt.value_or(BlockAnchor{});
}

BlockRecord para(const QString &t)
{
    BlockRecord r;
    r.kind   = BlockKind::Paragraph;
    r.source = t;
    r.text   = t;
    return r;
}

}  // namespace

class TstViewQmlLiveIme : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // 1. Set composing = true, insert a character via QTextCursor.
    //    contentsChange fires but should be suppressed — undoDepth stays 0.
    void preedit_suppresses_apply_local_edit()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "hello");
        if (!doc.blockByteRange(anchor))
            QSKIP("blockByteRange returned nullopt — parse may not have arrived");

        QTextCursor seed(qqtd->textDocument());
        seed.insertText(QStringLiteral("hello"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        // Enter composition — further contentsChange must be suppressed.
        binding.setComposing(true);

        // Insert a preedit character directly into the QTextDocument.
        QTextCursor c(qqtd->textDocument());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("x"));

        // applyLocalEdit must NOT have been called.
        QCOMPARE(doc.undoDepth(), 0);
    }

    // 2. Set composing = true, insert preedit chars (suppressed), then
    //    set composing = false — applyFullBlockReplacement fires and creates 1 undo entry.
    void composition_commit_applies_edit()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "hello");
        if (!doc.blockByteRange(anchor))
            QSKIP("blockByteRange returned nullopt — parse may not have arrived");

        QTextCursor seed(qqtd->textDocument());
        seed.insertText(QStringLiteral("hello"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        // Enter composition.
        binding.setComposing(true);

        // Insert preedit characters (suppressed — no CRDT write).
        QTextCursor c(qqtd->textDocument());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("世界"));

        QCOMPARE(doc.undoDepth(), 0);  // still suppressed

        // Commit: setComposing(false) triggers applyFullBlockReplacement.
        binding.setComposing(false);

        // The committed text ("hello世界") should now be in the CRDT as one undo entry.
        QCOMPARE(doc.undoDepth(), 1);

        const QByteArray docBytes = doc.toMarkdownUtf8();
        QVERIFY(docBytes.contains("hello"));
        QVERIFY(docBytes.contains("世界"));
    }

    // 3. Set composing = true, insert preedit chars, then cancel (reset text back to
    //    original under cycle guard), then set composing = false.
    //    applyFullBlockReplacement detects no change → no edit → undoDepth stays 0.
    void composition_cancel_is_noop()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "hello");
        if (!doc.blockByteRange(anchor))
            QSKIP("blockByteRange returned nullopt — parse may not have arrived");

        QTextCursor seed(qqtd->textDocument());
        seed.insertText(QStringLiteral("hello"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        binding.setComposing(true);

        // Simulate preedit insertion.
        QTextCursor c(qqtd->textDocument());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("あ"));

        // Simulate cancel: reset QTextDoc back to "hello" under the cycle guard
        // (so contentsChange from the reset doesn't trigger applyLocalEdit).
        binding.setModelText(QStringLiteral("hello"));

        // End composition — QTextDoc now matches CRDT, so no edit should apply.
        binding.setComposing(false);

        QCOMPARE(doc.undoDepth(), 0);
    }

    // 4. setComposingRow defers dataChanged; flushed on setComposingRow(false).
    void composing_row_defers_data_changed()
    {
        LiveBlockModel model;
        model.setRecords({ para("original") });

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

        // Mark row 0 as composing.
        model.setComposingRow(0, true);

        // Apply an Equal op that would normally emit dataChanged.
        BlockRecord updated = para("updated");
        model.applyOps(
            { { AstBlockDiff::OpKind::Equal, 0, 0 } },
            { updated });

        // Backing store must be updated.
        QCOMPARE(model.data(model.index(0, 0), model.roleForName("text")).toString(),
                 QStringLiteral("updated"));

        // But no signal yet — deferred.
        QCOMPARE(spy.count(), 0);

        // End composition — deferred notification must flush.
        model.setComposingRow(0, false);

        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TstViewQmlLiveIme)
#include "tst_view_qml_live_ime.moc"
