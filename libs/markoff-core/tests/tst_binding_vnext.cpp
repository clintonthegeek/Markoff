// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <memory>
#include <stdexcept>

namespace {
struct Fixture {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

Fixture openDocument(const QByteArray& markdown)
{
    auto provider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("failed to create ephemeral provider");
    Fixture fixture{std::move(provider).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider,
        {QStringLiteral("binding-vnext"), 1}, Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("failed to open provider-backed document");
    fixture.document = std::move(opened).value();
    const auto imported = fixture.document->importInitialMarkdown(markdown);
    if (imported.state != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("failed to import initial Markdown");
    return fixture;
}

void pumpEvents() { QCoreApplication::processEvents(); }

int intAttr(const QHash<Markoff::AttrName, Markoff::AttrValue>& attrs,
            const Markoff::AttrName& name)
{
    const auto it = attrs.constFind(name);
    if (it == attrs.cend()) return -1;
    const auto* value = std::get_if<int>(&it.value());
    return value ? *value : -1;
}

QString qstrAttr(const QHash<Markoff::AttrName, Markoff::AttrValue>& attrs,
                 const Markoff::AttrName& name)
{
    const auto it = attrs.constFind(name);
    if (it == attrs.cend()) return {};
    const auto* value = std::get_if<QString>(&it.value());
    return value ? *value : QString();
}

int boolAttr(const QHash<Markoff::AttrName, Markoff::AttrValue>& attrs,
             const Markoff::AttrName& name)
{
    const auto it = attrs.constFind(name);
    if (it == attrs.cend()) return -1;
    const auto* value = std::get_if<bool>(&it.value());
    return value ? (*value ? 1 : 0) : -1;
}
}

class TstBindingVNext final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void localEditPublishesToProviderDocument()
    {
        auto fixture = openDocument(QByteArrayLiteral("alpha\n\nbeta"));
        QSignalSpy actionSpy(fixture.document.get(),
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QVERIFY(actionSpy.isValid());
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());

        QTextCursor cursor(edit.document());
        cursor.setPosition(2);
        cursor.insertText(QStringLiteral("X"));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks().front()),
                 QByteArrayLiteral("alXpha"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("alXpha\nbeta"));
        QCOMPARE(actionSpy.size(), 1);
        const auto published = actionSpy.takeFirst();
        QVERIFY(!published.at(0).toByteArray().isEmpty());
        const QByteArray mabBytes = published.at(1).toByteArray();
        QCOMPARE(mabBytes.left(4), QByteArray("MAB\x03", 4));
    }

    void separatorDeletionMergesProviderBlocks()
    {
        auto fixture = openDocument(QByteArrayLiteral("hello\n\nworld"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());
        QCOMPARE(edit.toPlainText(), QStringLiteral("hello\nworld"));

        QTextCursor cursor(edit.document());
        cursor.setPosition(3);
        cursor.setPosition(8, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();

        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(1));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks().front()),
                 QByteArrayLiteral("helrld"));

        fixture.document->undoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(2));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[0]),
                 QByteArrayLiteral("hello"));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[1]),
                 QByteArrayLiteral("world"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("hello\nworld"));
        fixture.document->redoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(1));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks().front()),
                 QByteArrayLiteral("helrld"));

        binding.setMarkoffDocument(nullptr);
        fixture.document.reset();
        auto reopened = Markoff::MarkoffDocument::open(*fixture.provider,
            {QStringLiteral("binding-vnext"), 1}, Markoff::VNext::StorageMode::ephemeral);
        QVERIFY(reopened.ok());
        fixture.document = std::move(reopened).value();
        binding.setMarkoffDocument(fixture.document.get());
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(1));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks().front()),
                 QByteArrayLiteral("helrld"));
        fixture.document->undoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(2));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[0]),
                 QByteArrayLiteral("hello"));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[1]),
                 QByteArrayLiteral("world"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("hello\nworld"));
    }

    void remoteBlockEditReverseSyncs()
    {
        auto fixture = openDocument(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());
        const auto first = fixture.document->iterateBlocks().front();
        const auto result = fixture.document->applyBlockEdit(
            {first, 0, 0, QByteArrayLiteral("X")});
        QCOMPARE(result.state, Markoff::VNext::ActionPublicationState::committed);
        pumpEvents();
        QCOMPARE(edit.toPlainText(), QStringLiteral("Xalpha\nbeta"));
    }

    void remoteEditRestoresSelectionFromScopedTokens()
    {
        auto fixture = openDocument(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        QObject::connect(&binding,
            &Markoff::SourceTextDocumentBinding::caretResolved, &edit,
            [&edit](int anchor, int active) {
                QTextCursor restored(edit.document());
                restored.setPosition(anchor);
                if (active != anchor)
                    restored.setPosition(active, QTextCursor::KeepAnchor);
                edit.setTextCursor(restored);
            });
        binding.setMarkoffDocument(fixture.document.get());

        QTextCursor cursor(edit.document());
        cursor.setPosition(2);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        edit.setTextCursor(cursor);
        binding.captureViewSelection(cursor.anchor(), cursor.position());

        const auto first = fixture.document->iterateBlocks().front();
        const auto result = fixture.document->applyBlockEdit(
            {first, 0, 0, QByteArrayLiteral("X")});
        QCOMPARE(result.state, Markoff::VNext::ActionPublicationState::committed);
        pumpEvents();

        QCOMPARE(edit.toPlainText(), QStringLiteral("Xalpha\nbeta"));
        QCOMPARE(edit.textCursor().selectedText(), QStringLiteral("pha"));
        QCOMPARE(edit.textCursor().anchor(), 3);
        QCOMPARE(edit.textCursor().position(), 6);
    }

    void deletedSelectionEndpointIsNotRestoredAsBlockEnd()
    {
        auto fixture = openDocument(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());
        QSignalSpy caretSpy(&binding,
            &Markoff::SourceTextDocumentBinding::caretResolved);
        binding.captureViewSelection(2, 5);

        const auto first = fixture.document->iterateBlocks().front();
        const auto removed = fixture.document->applyStructural(
            Markoff::StructuralOp{Markoff::StructuralOp::RemoveEntry{first}});
        QCOMPARE(removed.state, Markoff::VNext::ActionPublicationState::committed);
        pumpEvents();

        QCOMPARE(edit.toPlainText(), QStringLiteral("beta"));
        QCOMPARE(caretSpy.count(), 0);
    }

    void sessionSelectionPersistsScopedTokensAndReattaches()
    {
        auto fixture = openDocument(QByteArrayLiteral("alpha\n\nbeta"));
        auto* savedSession = fixture.document->createSession();
        QPlainTextEdit firstEdit;
        Markoff::SourceTextDocumentBinding firstBinding;
        firstBinding.setTextDocument(firstEdit.document());
        firstBinding.setMarkoffDocument(fixture.document.get());
        firstBinding.setSession(savedSession);

        QTextCursor firstCursor(firstEdit.document());
        firstCursor.setPosition(2);
        firstCursor.setPosition(5, QTextCursor::KeepAnchor);
        firstEdit.setTextCursor(firstCursor);
        firstBinding.captureViewSelection(firstCursor.anchor(), firstCursor.position());
        QVERIFY(savedSession->primarySelection().anchorPosition.has_value());
        QVERIFY(savedSession->primarySelection().activePosition.has_value());

        auto corrupted = savedSession->primarySelection().toJson();
        auto corruptAnchor = corrupted.value("anchorPosition").toObject();
        corruptAnchor.insert("token", QStringLiteral("%%%"));
        corrupted.insert("anchorPosition", corruptAnchor);
        const auto rejected = Markoff::Selection::fromJson(corrupted);
        QVERIFY(!rejected.anchorPosition.has_value());

        auto* restoredSession = fixture.document->createSession();
        restoredSession->fromJson(savedSession->toJson());
        QPlainTextEdit secondEdit;
        Markoff::SourceTextDocumentBinding secondBinding;
        secondBinding.setTextDocument(secondEdit.document());
        QObject::connect(&secondBinding,
            &Markoff::SourceTextDocumentBinding::caretResolved, &secondEdit,
            [&secondEdit](int anchor, int active) {
                QTextCursor restored(secondEdit.document());
                restored.setPosition(anchor);
                if (active != anchor)
                    restored.setPosition(active, QTextCursor::KeepAnchor);
                secondEdit.setTextCursor(restored);
            });
        secondBinding.setMarkoffDocument(fixture.document.get());
        secondBinding.setSession(restoredSession);

        QCOMPARE(secondEdit.textCursor().selectedText(), QStringLiteral("pha"));
        QCOMPARE(secondEdit.textCursor().anchor(), 2);
        QCOMPARE(secondEdit.textCursor().position(), 5);
    }

    void enterSplitsTextAsOneUndoableAction()
    {
        auto fixture = openDocument(QByteArrayLiteral("hello world"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());
        QSignalSpy caretSpy(&binding, &Markoff::SourceTextDocumentBinding::caretResolved);

        QVERIFY(binding.handleStructuralKey(Qt::Key_Return, Qt::NoModifier, 5, 5));
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(2));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[0]),
                 QByteArrayLiteral("hello"));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[1]),
                 QByteArrayLiteral(" world"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("hello\n world"));
        QCOMPARE(caretSpy.count(), 1);

        fixture.document->undoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(1));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks().front()),
                 QByteArrayLiteral("hello world"));
        fixture.document->redoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(2));
    }

    void enterAfterSelectionDeletionSplitsAtCollapse()
    {
        auto fixture = openDocument(QByteArrayLiteral("- on=DELETE=two\n"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());
        QVERIFY(binding.handleStructuralKey(Qt::Key_Return, Qt::NoModifier, 10, 2));
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(2));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[0]),
                 QByteArrayLiteral("on"));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[1]),
                 QByteArrayLiteral("two"));
    }

    void enter_at_document_startInsertsBeforeExistingBlock()
    {
        auto fixture = openDocument(QByteArrayLiteral("hello"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());
        QVERIFY(binding.handleStructuralKey(Qt::Key_Return, Qt::NoModifier, 0, 0));
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(2));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[0]),
                 QByteArray{});
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[1]),
                 QByteArrayLiteral("hello"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("\nhello"));
    }

    void multilinePasteCreatesBlocksAndRestoresTargetTailAtomically()
    {
        auto fixture = openDocument(QByteArrayLiteral("hello world"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());
        QSignalSpy actionSpy(fixture.document.get(),
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QSignalSpy caretSpy(&binding,
            &Markoff::SourceTextDocumentBinding::caretResolved);

        QTextCursor cursor(edit.document());
        cursor.setPosition(6);
        cursor.insertText(QStringLiteral("A\n\nB"));

        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t(3));
        QCOMPARE(fixture.document->blockText(blocks[0]), QByteArrayLiteral("hello "));
        QCOMPARE(fixture.document->blockText(blocks[1]), QByteArrayLiteral("A"));
        QCOMPARE(fixture.document->blockText(blocks[2]), QByteArrayLiteral("Bworld"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("hello \nA\nBworld"));
        QCOMPARE(actionSpy.size(), 1);
        QCOMPARE(caretSpy.size(), 1);
        QCOMPARE(caretSpy.constLast().at(0).toInt(), 10);
        QCOMPARE(caretSpy.constLast().at(1).toInt(), 10);

        fixture.document->undoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(1));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks().front()),
                 QByteArrayLiteral("hello world"));
        fixture.document->redoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(3));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[2]),
                 QByteArrayLiteral("Bworld"));
    }

    void multilinePasteReplacesSelectedRangeInOneAction()
    {
        auto fixture = openDocument(QByteArrayLiteral("hello world"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());
        QSignalSpy actionSpy(fixture.document.get(),
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted);

        QTextCursor cursor(edit.document());
        cursor.setPosition(3);
        cursor.setPosition(8, QTextCursor::KeepAnchor);
        cursor.insertText(QStringLiteral("A\n\nB"));

        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t(3));
        QCOMPARE(fixture.document->blockText(blocks[0]), QByteArrayLiteral("hel"));
        QCOMPARE(fixture.document->blockText(blocks[1]), QByteArrayLiteral("A"));
        QCOMPARE(fixture.document->blockText(blocks[2]), QByteArrayLiteral("Brld"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("hel\nA\nBrld"));
        QCOMPARE(actionSpy.size(), 1);

        fixture.document->undoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(1));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks().front()),
                 QByteArrayLiteral("hello world"));
        fixture.document->redoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(3));
        QCOMPARE(fixture.document->blockText(fixture.document->iterateBlocks()[2]),
                 QByteArrayLiteral("Brld"));
    }

    // Enter inside a list item continues the run: the split-off item keeps
    // the indent level and the marker style, one published action / one
    // undo step. (Before the continuation attrs, the new item fell back to
    // the kind defaults — an unindented top-level "-" bullet.)
    void bulletContinuationEnterKeepsIndentLevelAndMarker()
    {
        auto fixture = openDocument(QByteArrayLiteral("- one\n\npara"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());

        const auto before = fixture.document->iterateBlocks();
        QVERIFY(fixture.document->blockKind(before[0]) == Markoff::BlockKind::ListItem);
        QCOMPARE(fixture.document->setBlockAttribute(before[0],
                   Markoff::AttrNames::IndentLevel, 2).state,
                 Markoff::VNext::ActionPublicationState::committed);
        pumpEvents();

        QSignalSpy actionSpy(fixture.document.get(),
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QVERIFY(binding.handleStructuralKey(Qt::Key_Return, Qt::NoModifier, 3, 3));
        pumpEvents();

        const auto after = fixture.document->iterateBlocks();
        QCOMPARE(after.size(), size_t(3));  // item, continuation, para
        QCOMPARE(fixture.document->blockText(after[0]), QByteArrayLiteral("one"));
        QCOMPARE(fixture.document->blockText(after[1]), QByteArray{});
        QVERIFY(fixture.document->blockKind(after[1]) == Markoff::BlockKind::ListItem);
        const auto attrs = fixture.document->blockAttrs(after[1]);
        QCOMPARE(intAttr(attrs, Markoff::AttrNames::IndentLevel), 2);
        QCOMPARE(qstrAttr(attrs, Markoff::AttrNames::MarkerStyle), QStringLiteral("minus"));
        // One published action, one undo step.
        QCOMPARE(actionSpy.size(), 1);
        fixture.document->undoD2();
        pumpEvents();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t(2));
        QCOMPARE(intAttr(fixture.document->blockAttrs(before[0]),
                         Markoff::AttrNames::IndentLevel), 2);
    }

    void orderedContinuationEnterAdvancesTheMarkerNumber()
    {
        auto fixture = openDocument(QByteArrayLiteral("1. one\n\npara"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());

        QVERIFY(binding.handleStructuralKey(Qt::Key_Return, Qt::NoModifier, 3, 3));
        pumpEvents();

        const auto after = fixture.document->iterateBlocks();
        QCOMPARE(after.size(), size_t(3));
        QVERIFY(fixture.document->blockKind(after[1]) == Markoff::BlockKind::ListItem);
        const auto attrs = fixture.document->blockAttrs(after[1]);
        QCOMPARE(qstrAttr(attrs, Markoff::AttrNames::MarkerStyle), QStringLiteral("dot"));
        QCOMPARE(intAttr(attrs, Markoff::AttrNames::MarkerNumber), 2);
        QCOMPARE(intAttr(attrs, Markoff::AttrNames::IndentLevel), 0);
    }

    // A task item's continuation starts UNchecked: Checked is never copied,
    // and the fresh `false` write must exist so the checkbox stays
    // clickable (toggleListItemChecked refuses items without the attr).
    void taskContinuationEnterStartsUncheckedAndKeepsIndent()
    {
        auto fixture = openDocument(QByteArrayLiteral("- [x] done\n\npara"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding binding;
        binding.setTextDocument(edit.document());
        binding.setMarkoffDocument(fixture.document.get());

        const auto before = fixture.document->iterateBlocks();
        QCOMPARE(boolAttr(fixture.document->blockAttrs(before[0]),
                          Markoff::AttrNames::Checked), 1);
        QVERIFY(fixture.document->setBlockAttribute(before[0],
                   Markoff::AttrNames::IndentLevel, 1).state
                == Markoff::VNext::ActionPublicationState::committed);
        pumpEvents();

        QVERIFY(binding.handleStructuralKey(Qt::Key_Return, Qt::NoModifier, 4, 4));
        pumpEvents();

        const auto after = fixture.document->iterateBlocks();
        QCOMPARE(after.size(), size_t(3));
        QCOMPARE(fixture.document->blockText(after[0]), QByteArrayLiteral("done"));
        const auto attrs = fixture.document->blockAttrs(after[1]);
        QCOMPARE(qstrAttr(attrs, Markoff::AttrNames::MarkerStyle), QStringLiteral("task"));
        QCOMPARE(boolAttr(attrs, Markoff::AttrNames::Checked), 0);
        QCOMPARE(intAttr(attrs, Markoff::AttrNames::IndentLevel), 1);
    }
};

QTEST_MAIN(TstBindingVNext)
#include "tst_binding_vnext.moc"
