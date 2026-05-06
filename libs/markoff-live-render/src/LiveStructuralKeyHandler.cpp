// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveStructuralKeyHandler.h>

#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/UndoLog.h>
#include <markoff-foundation/Cmd/D2.h>
#include <markoff-foundation/AttrNames.h>

#include <QLoggingCategory>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcStruct, "markoff.live.struct", QtWarningMsg)

namespace Markoff::LiveRender {

namespace {

/// Marker design (spec §0) is bounded to "paragraph holes only" — list items,
/// blockquotes, and similar block-likes are out of scope. R2's BlockWalker
/// collapses lists/blockquotes/HTML/etc. into a single row whose `kind` is
/// `BlockKind::Paragraph` but whose `text` carries the source-faithful
/// markdown including the list/quote markers. Without a guard, paragraphEnter
/// fires its mid-block-split logic on that row and destructively rewrites the
/// source (e.g. injects a paragraph break between two list items).
///
/// Strategy B (text-pattern heuristic): treat a row as non-paragraph if its
/// text starts with a markdown list marker (`-`, `*`, `+`, `1.`, `1)`) or a
/// blockquote marker (`>`). Returning `HR::NotHandled` from paragraphEnter in
/// that case lets QTextEdit's default Enter handling apply — a literal `\n`
/// soft-break inside the row, which the parser keeps as a soft line break
/// inside the existing list/quote. Non-destructive.
bool rowIsListOrQuoteContent(const QString &blockText)
{
    static const QRegularExpression kListOrQuotePrefix(
        QStringLiteral(R"(^[ \t]{0,3}(?:[-*+]|\d{1,9}[.)])\s)"));
    static const QRegularExpression kBlockQuotePrefix(
        QStringLiteral(R"(^[ \t]{0,3}>)"));
    return kListOrQuotePrefix.match(blockText).hasMatch()
        || kBlockQuotePrefix.match(blockText).hasMatch();
}

}  // namespace

LiveStructuralKeyHandler::LiveStructuralKeyHandler(
    Markoff::MarkoffDocument *document,
    LiveBlockModel           *model,
    LiveCursorState          *cursorState,
    const BlockKindRegistry  *registry,
    QObject                  *parent)
    : QObject(parent)
    , m_document(document)
    , m_model(model)
    , m_cursorState(cursorState)
    , m_registry(registry)
{
    registerBuiltins();
}

void LiveStructuralKeyHandler::registerHandler(const QString &kind, int key, HandlerFn fn)
{
    m_handlers[kind][key] = std::move(fn);
}

bool LiveStructuralKeyHandler::tryHandle(int key,
                                         int modifiers,
                                         int blockIndex,
                                         int qtPos,
                                         bool selectionEmpty,
                                         const QString &blockText)
{
    qCDebug(lcStruct) << "tryHandle key=" << key
                      << "mod=" << modifiers
                      << "blockIndex=" << blockIndex
                      << "qtPos=" << qtPos
                      << "blockTextLen=" << blockText.length()
                      << "selEmpty=" << selectionEmpty;
    if (!m_document || !m_model || !m_cursorState || !m_registry) return false;
    if (!selectionEmpty) {
        // R5 limitation: non-empty selection defers to TextEdit's native
        // selection-replacement (which routes through LiveEditBinding's
        // contentsChange path).
        return false;
    }

    if (blockIndex < 0 || blockIndex >= m_model->rowCount()) return false;

    const BlockRecord &rec = m_model->recordAt(blockIndex);
    const auto *desc = m_registry->find(rec.kind);
    if (!desc) return false;

    // Heading level-change: Ctrl+Shift+0-6 before consuming from the keys set.
    // Strategy: rewrite the leading `# ` prefix in the block text so that
    // onD2Changed's text-based inference picks up the new level naturally.
    // For level=0 (demote to paragraph) we strip the prefix and set the block
    // kind to Paragraph.
    if (rec.kind == BlockKind::Heading
            && (modifiers & Qt::ControlModifier) && (modifiers & Qt::ShiftModifier)
            && key >= Qt::Key_0 && key <= Qt::Key_6) {
        const int newLevel = key - Qt::Key_0;
        const Markoff::BlockId id(rec.blockAnchor);

        // Compute the existing `# ` prefix length from blockText.
        // Heading text is stored as "## Hello" (source-faithful, with # prefix).
        const QByteArray textUtf8 = blockText.toUtf8();
        int oldPrefixLen = 0;
        while (oldPrefixLen < textUtf8.size() && textUtf8[oldPrefixLen] == '#')
            ++oldPrefixLen;
        // Skip one trailing space after the hashes, if present.
        const int spaceAfterHash = (oldPrefixLen < textUtf8.size()
                                    && textUtf8[oldPrefixLen] == ' ') ? 1 : 0;
        const int oldPrefixBytes = oldPrefixLen + spaceAfterHash;

        auto &undoLog = m_document->d2UndoLog();
        UndoLog::Transaction t(undoLog);

        if (newLevel == 0) {
            // Demote to paragraph: remove the entire `## ` prefix, change kind.
            m_document->d2ApplyBufferEdit(id, 0,
                                          static_cast<uint32_t>(oldPrefixBytes),
                                          QByteArray{}, t);
            m_document->d2SetBlockKind(id, Markoff::BlockKind::Paragraph, t);
        } else {
            // Build new prefix: newLevel hashes + space.
            QByteArray newPrefix(newLevel, '#');
            newPrefix.append(' ');
            m_document->d2ApplyBufferEdit(id, 0,
                                          static_cast<uint32_t>(oldPrefixBytes),
                                          newPrefix, t);
        }
        return true;
    }

    if (!desc->consumedStructuralKeys.contains(key)) return false;

    auto kindIt = m_handlers.constFind(rec.kind);
    if (kindIt == m_handlers.constEnd()) return false;
    auto keyIt = kindIt.value().constFind(key);
    if (keyIt == kindIt.value().constEnd()) return false;

    Ctx ctx;
    ctx.document          = m_document.data();
    ctx.model             = m_model;
    ctx.cursorState       = m_cursorState;
    ctx.blockIndex        = blockIndex;
    ctx.blockAnchor       = rec.blockAnchor;
    ctx.qtPos             = qtPos;
    ctx.modifiers         = modifiers;
    ctx.blockText         = blockText;

    return keyIt.value()(ctx) == HandleResult::Handled;
}

void LiveStructuralKeyHandler::changeCodeLanguage(Markoff::BlockAnchor anchor,
                                                    const QString &lang)
{
    if (!m_document) return;
    const Markoff::BlockId id(anchor);
    UndoLog::Transaction t(m_document->d2UndoLog());
    m_document->d2SetBlockAttr(id, Markoff::AttrNames::InfoString, lang, t);
}

void LiveStructuralKeyHandler::changeImageAlt(Markoff::BlockAnchor anchor,
                                               const QString &alt)
{
    if (!m_document) return;
    const Markoff::BlockId id(anchor);
    UndoLog::Transaction t(m_document->d2UndoLog());
    m_document->d2SetBlockAttr(id, Markoff::AttrNames::Alt, alt, t);
}

void LiveStructuralKeyHandler::registerBuiltins()
{
    using HR = HandleResult;

    // ---------- paragraph: Enter (all positions, Shift-aware) ----------
    auto paragraphEnter = [](const Ctx &c) -> HR {
        const bool isShift = (c.modifiers & Qt::ShiftModifier) != 0;

        // Bug 1 gate: marker design is bounded to top-level paragraphs.
        // R2's BlockWalker collapses lists/blockquotes into a Paragraph-
        // kinded row; without this guard, Enter splices the row with a
        // paragraph break, destructively splitting the list.
        if (rowIsListOrQuoteContent(c.blockText)) {
            return HR::NotHandled;
        }

        if (isShift) {
            // Soft break — insert \n within the block, stay in same block.
            // D2: use Cmd::insertSoftBreak with within-block byte offset.
            const QByteArray prefixUtf8 = c.blockText.left(c.qtPos).toUtf8();
            const uint32_t byteOff = static_cast<uint32_t>(prefixUtf8.size());
            Markoff::Cmd::insertSoftBreak(*c.document, c.blockAnchor, byteOff);
            c.cursorState->requestTextCaretAtRow(c.blockIndex, c.qtPos + 1);
            return HR::Handled;
        }

        const bool atStart = (c.qtPos == 0);
        const bool atEnd   = (c.qtPos == c.blockText.length());

        if (atEnd) {
            // EOB Enter: create a new paragraph block after the current one.
            // D2: use Cmd::enterAtEnd.
            Markoff::BlockId newBlock = Markoff::Cmd::enterAtEnd(*c.document, c.blockAnchor);
            Q_UNUSED(newBlock)
            // The new block will appear at blockIndex+1 once structureChanged fires.
            c.cursorState->requestTextCaretAtNewRow(c.blockIndex + 1, 0);
            return HR::Handled;
        }

        if (atStart) {
            // SOB Enter: insert a new empty paragraph BEFORE the current block.
            // Strategy: if there's a previous block, use Cmd::enterAtEnd on it
            // (which inserts after it, i.e. before the current block). Otherwise
            // use d2InsertBlock at the head.
            Markoff::BlockId newBlock;
            if (c.blockIndex > 0) {
                const Markoff::BlockAnchor prevAnchor =
                    c.model->recordAt(c.blockIndex - 1).blockAnchor;
                newBlock = Markoff::Cmd::enterAtEnd(*c.document, prevAnchor);
            } else {
                // Insert before the first block: insert after null block.
                auto &undoLog = c.document->d2UndoLog();
                UndoLog::Transaction t(undoLog);
                newBlock = c.document->d2InsertBlock(Markoff::BlockId{},
                                                     Markoff::BlockKind::Paragraph, t);
            }
            Q_UNUSED(newBlock)
            // Cursor stays at the current visual row (blockIndex) — the new
            // empty block occupies that row after the insert.
            c.cursorState->requestTextCaretAtNewRow(c.blockIndex, 0);
            return HR::Handled;
        }

        // Mid-block split: split at qtPos.
        // D2: truncate the current block, insert new block after, set its text
        // to the suffix — all in a single transaction.
        {
            const QByteArray fullUtf8   = c.blockText.toUtf8();
            const QByteArray prefixUtf8 = c.blockText.left(c.qtPos).toUtf8();
            const QByteArray suffixUtf8 = c.blockText.mid(c.qtPos).toUtf8();
            const uint32_t byteOff      = static_cast<uint32_t>(prefixUtf8.size());
            const uint32_t tailBytes    = static_cast<uint32_t>(
                fullUtf8.size() - prefixUtf8.size());

            auto &undoLog = c.document->d2UndoLog();
            UndoLog::Transaction t(undoLog);

            // 1. Truncate current block to prefix.
            c.document->d2ApplyBufferEdit(c.blockAnchor, byteOff, tailBytes,
                                          QByteArray{}, t);

            // 2. Insert new block after current.
            Markoff::BlockId newBlock = c.document->d2InsertBlock(
                c.blockAnchor, Markoff::BlockKind::Paragraph, t);

            // 3. Set new block's text to suffix.
            c.document->d2ApplyBufferEdit(newBlock, 0, 0, suffixUtf8, t);

            // Cursor goes to start of new block.
            c.cursorState->requestTextCaretAtNewRow(c.blockIndex + 1, 0);
        }
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Return] = paragraphEnter;
    m_handlers[BlockKind::Paragraph][Qt::Key_Enter]  = paragraphEnter;

    auto paragraphBackspace = [](const Ctx &c) -> HR {
        if (c.qtPos != 0) return HR::NotHandled;     // not at row-start
        if (c.blockIndex == 0) return HR::NotHandled; // first block

        // Compute join position BEFORE the merge while the model is still in
        // the pre-merge state. The join point is the character-count end of the
        // preceding block's text (which has its trailing '\n' stripped by the
        // model's display layer).
        const int joinQtPos = c.model->recordAt(c.blockIndex - 1).text.length();

        auto result = Markoff::Cmd::backspaceMerge(*c.document, c.blockAnchor);
        if (result.mergedInto.isNull()) return HR::NotHandled;

        // Use anchor-keyed pending so the cursor resolves in noteParseArrived,
        // AFTER applyOps has updated the model text and QML has processed it.
        // requestTextCaretAtRow resolves immediately (row exists), but the text
        // update then resets QML's cursor to the end — anchor-keyed pending
        // avoids that race.
        c.cursorState->requestTextCaretAtAnchor(result.mergedInto, joinQtPos);

        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Backspace] = paragraphBackspace;

    auto paragraphDelete = [](const Ctx &c) -> HR {
        if (c.qtPos != c.blockText.length()) return HR::NotHandled;
        if (c.blockIndex >= c.model->rowCount() - 1) return HR::NotHandled;

        // D2: use Cmd::deleteMerge (merges next block into current).
        Markoff::Cmd::deleteMerge(*c.document, c.blockAnchor);

        // Same race as backspace: requestTextCaretAtRow resolves before the
        // model text is updated, then QML resets the cursor. Use anchor-keyed
        // pending so it resolves in noteParseArrived after the text is stable.
        c.cursorState->requestTextCaretAtAnchor(c.blockAnchor, c.qtPos);

        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Delete] = paragraphDelete;

    // Heading: same handlers as paragraph. Source-faithful text means the
    // # prefix is part of blockText, so qtPos arithmetic matches paragraph.
    m_handlers[BlockKind::Heading][Qt::Key_Return]    = paragraphEnter;
    m_handlers[BlockKind::Heading][Qt::Key_Enter]     = paragraphEnter;
    m_handlers[BlockKind::Heading][Qt::Key_Backspace] = paragraphBackspace;
    m_handlers[BlockKind::Heading][Qt::Key_Delete]    = paragraphDelete;

    // Code-block: only the merge handlers. Enter is NOT consumed —
    // TextEdit's native \n insertion routes through LiveEditBinding as
    // a regular text edit, producing the literal newline the user
    // expects inside fenced code.
    m_handlers[BlockKind::CodeBlock][Qt::Key_Backspace] = paragraphBackspace;
    m_handlers[BlockKind::CodeBlock][Qt::Key_Delete]    = paragraphDelete;

    // CodeBlock Tab: insert 4 spaces at cursor position.
    m_handlers[BlockKind::CodeBlock][Qt::Key_Tab] = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        const QByteArray spaces("    ");  // 4 spaces
        UndoLog::Transaction t(c.document->d2UndoLog());
        c.document->d2ApplyBufferEdit(id,
                                       static_cast<uint32_t>(c.qtPos),
                                       0,
                                       spaces,
                                       t);
        c.cursorState->requestTextCaretAtRow(c.blockIndex, c.qtPos + 4);
        return HR::Handled;
    };

    // HorizontalRule: Up/Down navigation, Delete/Backspace removes the block.
    auto hrNavigate = [](const Ctx &c, int targetRow) -> HR {
        if (targetRow < 0 || targetRow >= c.model->rowCount()) return HR::NotHandled;
        c.cursorState->requestTextCaretAtRow(targetRow,
            (targetRow < c.model->rowCount()) ? c.model->recordAt(targetRow).text.length() : 0);
        return HR::Handled;
    };

    m_handlers[BlockKind::HorizontalRule][Qt::Key_Up] = [hrNavigate](const Ctx &c) {
        return hrNavigate(c, c.blockIndex - 1);
    };
    m_handlers[BlockKind::HorizontalRule][Qt::Key_Down] = [hrNavigate](const Ctx &c) {
        return hrNavigate(c, c.blockIndex + 1);
    };

    auto hrDelete = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        const int targetRow = std::max(0, c.blockIndex - 1);
        UndoLog::Transaction t(c.document->d2UndoLog());
        c.document->d2RemoveBlock(id, t);
        // Resolve cursor AFTER delete — model will reflect removal after d2DocumentChanged fires.
        // Use requestTextCaretAtRow; if targetRow was before the deleted row, its index is stable.
        if (c.model->rowCount() > 1) {
            const int resolveRow = std::min(targetRow, c.model->rowCount() - 1);
            c.cursorState->requestTextCaretAtRow(resolveRow,
                c.model->recordAt(resolveRow).text.length());
        }
        return HR::Handled;
    };
    m_handlers[BlockKind::HorizontalRule][Qt::Key_Delete]    = hrDelete;
    m_handlers[BlockKind::HorizontalRule][Qt::Key_Backspace] = hrDelete;

    // ---- ListItem handlers ----

    // Enter: insert new ListItem after current, or exit list on empty item.
    m_handlers[BlockKind::ListItem][Qt::Key_Return] =
    m_handlers[BlockKind::ListItem][Qt::Key_Enter]  = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);

        // Extract the marker prefix (e.g. "- ", "* ", "1. ").
        QByteArray markerPrefix;
        {
            static const QRegularExpression kMarker(
                QStringLiteral(R"(^[ \t]{0,3}(?:[-*+]|\d{1,9}[.)]) )"));
            auto m = kMarker.match(c.blockText);
            markerPrefix = m.hasMatch()
                ? c.blockText.left(m.capturedLength()).toUtf8()
                : QByteArray("- ");
        }
        // "Empty" = text is exactly the marker prefix (no content after it).
        const bool isEmpty = (c.blockText.toUtf8().trimmed() == markerPrefix.trimmed());

        // Determine indentLevel from attrs.
        const auto attrs = c.document->blockAttrs(id);
        const int indentLevel = attrs.contains(Markoff::AttrNames::IndentLevel)
            ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;

        if (isEmpty && indentLevel > 0) {
            // Outdent one level.
            UndoLog::Transaction t(c.document->d2UndoLog());
            c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                        indentLevel - 1, t);
            c.cursorState->requestTextCaretAtRow(c.blockIndex,
                static_cast<int>(markerPrefix.length()));
            return HR::Handled;
        }
        if (isEmpty && indentLevel == 0) {
            // Exit list: demote to Paragraph and strip the marker prefix.
            UndoLog::Transaction t(c.document->d2UndoLog());
            if (!markerPrefix.isEmpty())
                c.document->d2ApplyBufferEdit(id, 0,
                    static_cast<uint32_t>(markerPrefix.length()), {}, t);
            c.document->d2SetBlockKind(id, Markoff::BlockKind::Paragraph, t);
            c.cursorState->requestTextCaretAtRow(c.blockIndex, 0);
            return HR::Handled;
        }
        // Non-empty: insert new ListItem after current with same marker style.
        // Insert directly as ListItem so onD2Changed sees the correct kind
        // immediately (no kind-transition round-trip needed).
        UndoLog::Transaction t(c.document->d2UndoLog());
        const Markoff::BlockId newId =
            c.document->d2InsertBlock(id, Markoff::BlockKind::ListItem, t);
        c.document->d2ApplyBufferEdit(newId, 0, 0, markerPrefix, t);
        // Copy indent level to new block.
        if (indentLevel > 0)
            c.document->d2SetBlockAttr(newId, Markoff::AttrNames::IndentLevel,
                                        indentLevel, t);
        c.cursorState->requestTextCaretAtAnchor(
            Markoff::BlockId(newId),
            static_cast<int>(markerPrefix.length()));
        return HR::Handled;
    };

    // Backspace: outdent if indented and at row-start, else merge with previous.
    m_handlers[BlockKind::ListItem][Qt::Key_Backspace] = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        const auto attrs = c.document->blockAttrs(id);
        const int indentLevel = attrs.contains(Markoff::AttrNames::IndentLevel)
            ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;
        if (c.qtPos == 0 && indentLevel > 0) {
            UndoLog::Transaction t(c.document->d2UndoLog());
            c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                        indentLevel - 1, t);
            c.cursorState->requestTextCaretAtRow(c.blockIndex, 0);
            return HR::Handled;
        }
        // qtPos > 0 or indentLevel == 0: normal merge with previous block.
        if (c.qtPos != 0) return HR::NotHandled;
        if (c.blockIndex == 0) return HR::NotHandled;
        const int joinQtPos = c.model->recordAt(c.blockIndex - 1).text.length();
        auto result = Markoff::Cmd::backspaceMerge(*c.document, c.blockAnchor);
        if (result.mergedInto.isNull()) return HR::NotHandled;
        c.cursorState->requestTextCaretAtAnchor(result.mergedInto, joinQtPos);
        return HR::Handled;
    };

    // Delete: merge with next block.
    m_handlers[BlockKind::ListItem][Qt::Key_Delete] = [](const Ctx &c) -> HR {
        if (c.qtPos < c.blockText.length()) return HR::NotHandled;
        if (c.blockIndex >= c.model->rowCount() - 1) return HR::NotHandled;
        Markoff::Cmd::deleteMerge(*c.document, c.blockAnchor);
        c.cursorState->requestTextCaretAtAnchor(c.blockAnchor, c.qtPos);
        return HR::Handled;
    };

    // Tab: indent; Shift+Tab: outdent.
    m_handlers[BlockKind::ListItem][Qt::Key_Tab] = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        const auto attrs = c.document->blockAttrs(id);
        const int indentLevel = attrs.contains(Markoff::AttrNames::IndentLevel)
            ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;
        UndoLog::Transaction t(c.document->d2UndoLog());
        const bool shift = (c.modifiers & Qt::ShiftModifier) != 0;
        if (shift) {
            if (indentLevel > 0)
                c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                            indentLevel - 1, t);
        } else {
            c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                        std::min(indentLevel + 1, 6), t);
        }
        c.cursorState->requestTextCaretAtRow(c.blockIndex, c.qtPos);
        return HR::Handled;
    };

    // Image: Backspace/Delete removes the block (same as HR).
    auto imgDelete = [](const Ctx &c) -> HandleResult {
        const Markoff::BlockId id(c.blockAnchor);
        const int targetRow = std::max(0, c.blockIndex - 1);
        UndoLog::Transaction t(c.document->d2UndoLog());
        c.document->d2RemoveBlock(id, t);
        if (c.model->rowCount() > 1) {
            const int resolveRow = std::min(targetRow, c.model->rowCount() - 2);
            c.cursorState->requestTextCaretAtRow(resolveRow,
                c.model->recordAt(resolveRow).text.length());
        }
        return HandleResult::Handled;
    };
    m_handlers[BlockKind::Image][Qt::Key_Delete]    = imgDelete;
    m_handlers[BlockKind::Image][Qt::Key_Backspace] = imgDelete;
}

}  // namespace Markoff::LiveRender
