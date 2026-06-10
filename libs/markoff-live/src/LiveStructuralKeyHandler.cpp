// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveStructuralKeyHandler.h>

#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/BlockKindDescriptor.h>
#include <markoff/live/BlockKind.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/Cursor.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/AttrNames.h>

#include <QLoggingCategory>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcStruct, "markoff.live.struct", QtWarningMsg)

namespace Markoff::Live {

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

void LiveStructuralKeyHandler::setReadOnlyProvider(std::function<bool()> provider)
{
    m_readOnlyProvider = std::move(provider);
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

    // Read-only gate (contract-v2 spec §4.2): mutating keys are consumed
    // without mutating — returning true also keeps the TextEdit's native
    // fallback (selection-replacement, literal '\n') from firing. Pure
    // navigation (Up/Down/F2/Escape) falls through untouched. Gated here,
    // at the top of dispatch, not per-rule.
    if (false && m_readOnlyProvider && m_readOnlyProvider()) {  // PROOF: gate disabled
        const bool headingLevelChord =
            (modifiers & Qt::ControlModifier) && (modifiers & Qt::ShiftModifier)
            && key >= Qt::Key_0 && key <= Qt::Key_6;
        const bool mutatingKey =
            key == Qt::Key_Return || key == Qt::Key_Enter
            || key == Qt::Key_Backspace || key == Qt::Key_Delete
            || key == Qt::Key_Tab || key == Qt::Key_Backtab
            || headingLevelChord;
        if (mutatingKey) return true;
    }

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

    // F2: enter BlockInternalEdit on kinds that support it.
    if (key == Qt::Key_F2) {
        if (desc && !desc->internalEditModes.isEmpty()
                 && desc->supportedCursorVariants.contains(QStringLiteral("BlockInternalEdit"))) {
            BlockInternalEdit bie;
            bie.block = rec.blockAnchor;
            bie.mode  = desc->internalEditModes.first();
            m_cursorState->request(bie);
            return true;
        }
        return false;
    }

    // Escape: exit BlockInternalEdit → BlockSelected.
    if (key == Qt::Key_Escape) {
        if (m_cursorState->cursorKind() == QStringLiteral("BlockInternalEdit")) {
            BlockSelected sel;
            sel.block = rec.blockAnchor;
            m_cursorState->request(sel);
            return true;
        }
        return false;
    }

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

    // Block-only fence (spec §4 R-backspace-at-text-start-adjacent /
    // R-delete-at-text-end-adjacent): Backspace at qtPos=0 or Delete at
    // qtPos=length, when the adjacent block is isBlockOnly, selects
    // the adjacent block instead of running the cross-boundary merge.
    // Fires for ANY text-bearing kind (Paragraph, Heading, ListItem, etc.)
    // before per-kind dispatch, so no per-kind handler needs to guard this.
    if ((key == Qt::Key_Backspace && qtPos == 0 && blockIndex > 0)
            || (key == Qt::Key_Delete && qtPos == blockText.length()
                && blockIndex < m_model->rowCount() - 1)) {
        const int adjRow = (key == Qt::Key_Backspace)
                               ? blockIndex - 1 : blockIndex + 1;
        const BlockRecord &adjRec = m_model->recordAt(adjRow);
        if (m_registry->isBlockOnly(adjRec.kind)) {
            // Use requestBlockSelected (not raw request) so focus is
            // delivered to the block-only delegate's root via takeFocus.
            // Without this, subsequent Backspace/Delete keeps firing
            // through the (now-focused) adjacent paragraph delegate,
            // which routes back through this same fence — the
            // user sees "BlockSelected promoted, but second key does
            // nothing" as a result.
            m_cursorState->requestBlockSelected(adjRec.blockAnchor);
            return true;
        }
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

            // Flush the queued d2DocumentChanged synchronously so the model
            // and the delegate's QTextDocument finish updating BEFORE we
            // request the new caret position. requestTextCaretAtRow resolves
            // synchronously when the target row already exists (which it
            // does for soft-break — same row, only text changed). Without
            // the flush, onCursorChanged fires against the pre-edit
            // QTextDocument; the later pushTextToDocument's setPlainText
            // reflows the cursor and lands the caret at end-of-block.
            c.document->flushPendingD2Changed();

            c.cursorState->establishFocus(c.blockAnchor, c.qtPos + 1);
            return HR::Handled;
        }

        const bool atStart = (c.qtPos == 0);
        const bool atEnd   = (c.qtPos == c.blockText.length());

        if (atEnd) {
            // EOB Enter: create a new paragraph block after the current one.
            // D2: use Cmd::enterAtEnd.
            Markoff::BlockId newBlock = Markoff::Cmd::enterAtEnd(*c.document, c.blockAnchor);
            c.cursorState->establishFocus(Markoff::BlockAnchor(newBlock), 0);
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
            c.cursorState->establishFocus(Markoff::BlockAnchor(newBlock), 0);
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

            c.cursorState->establishFocus(Markoff::BlockAnchor(newBlock), 0);
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

        c.cursorState->establishFocus(result.mergedInto, joinQtPos);

        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Backspace] = paragraphBackspace;

    auto paragraphDelete = [](const Ctx &c) -> HR {
        if (c.qtPos != c.blockText.length()) return HR::NotHandled;
        if (c.blockIndex >= c.model->rowCount() - 1) return HR::NotHandled;

        // D2: use Cmd::deleteMerge (merges next block into current).
        Markoff::Cmd::deleteMerge(*c.document, c.blockAnchor);

        c.cursorState->establishFocus(c.blockAnchor, c.qtPos);

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
        c.cursorState->establishFocus(c.blockAnchor, c.qtPos + 4);
        return HR::Handled;
    };

    // Block-only kinds (HR, Image, …): generic Up/Down navigation.
    // One arrow press = one step: land on the adjacent row. The chokepoint
    // (establishFocus → tryResolvePending) selects the correct cursor variant
    // (TextCaret for text-bearing, BlockSelected for block-only) so the
    // handler doesn't need to know the target kind.
    auto blockOnlyNavigateUp = [](const Ctx &c) -> HR {
        const int targetRow = c.blockIndex - 1;
        if (targetRow < 0) return HR::NotHandled;
        c.cursorState->establishFocus(c.model->recordAt(targetRow).blockAnchor,
            c.model->recordAt(targetRow).text.length());
        return HR::Handled;
    };
    auto blockOnlyNavigateDown = [](const Ctx &c) -> HR {
        const int targetRow = c.blockIndex + 1;
        if (targetRow >= c.model->rowCount()) return HR::NotHandled;
        c.cursorState->establishFocus(c.model->recordAt(targetRow).blockAnchor, 0);
        return HR::Handled;
    };

    // Generic block-only delete: remove the block, land cursor on the block above
    // (or the block below if this was the first block). Works for HR, Image, Math, …
    auto blockOnlyDelete = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        const int targetRow = std::max(0, c.blockIndex - 1);
        UndoLog::Transaction t(c.document->d2UndoLog());
        c.document->d2RemoveBlock(id, t);
        if (c.model->rowCount() > 0) {
            const int resolveRow = std::min(targetRow, c.model->rowCount() - 1);
            c.cursorState->establishFocus(c.model->recordAt(resolveRow).blockAnchor,
                c.model->recordAt(resolveRow).text.length());
        }
        return HR::Handled;
    };

    // Generic block-only enter: insert an empty Paragraph after the current
    // block-only block and land TextCaret on it at qtPos=0.
    auto blockOnlyEnter = [](const Ctx &c) -> HR {
        const Markoff::BlockId newBlock =
            Markoff::Cmd::enterAtEnd(*c.document, c.blockAnchor);
        c.cursorState->establishFocus(Markoff::BlockAnchor(newBlock), 0);
        return HR::Handled;
    };

    // Register Up/Down, Delete/Backspace, and Return/Enter for every
    // kind whose descriptor has isBlockOnly == true.
    for (const QString &kind : m_registry->kinds()) {
        if (!m_registry->isBlockOnly(kind)) continue;
        m_handlers[kind][Qt::Key_Up]        = blockOnlyNavigateUp;
        m_handlers[kind][Qt::Key_Down]      = blockOnlyNavigateDown;
        m_handlers[kind][Qt::Key_Delete]    = blockOnlyDelete;
        m_handlers[kind][Qt::Key_Backspace] = blockOnlyDelete;
        m_handlers[kind][Qt::Key_Return]    = blockOnlyEnter;
        m_handlers[kind][Qt::Key_Enter]     = blockOnlyEnter;
    }

    // ---- ListItem handlers ----
    // Per-item block model: each ListItem is its own CRDT block.
    // blockText = content only (no marker prefix); marker/indent are attrs.

    // Enter: split, exit list, or outdent — depending on content and cursor
    // position. All ops go on a single transaction (one undo step).
    m_handlers[BlockKind::ListItem][Qt::Key_Return] =
    m_handlers[BlockKind::ListItem][Qt::Key_Enter]  = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        const QString &content = c.blockText;  // content only, no marker

        const auto attrs = c.document->blockAttrs(id);
        const int indent = attrs.contains(Markoff::AttrNames::IndentLevel)
            ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;

        UndoLog::Transaction t(c.document->d2UndoLog());

        if (content.isEmpty() && indent > 0) {
            // Outdent: reduce indent level by one.
            c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                        indent - 1, t);
            Markoff::Cmd::renumberRunStartingAt(*c.document, id, t);
            c.cursorState->establishFocus(c.blockAnchor, 0);
            return HR::Handled;
        }

        if (content.isEmpty() && indent == 0) {
            // Exit list: demote to Paragraph, clear MarkerStyle.
            c.document->d2SetBlockKind(id, Markoff::BlockKind::Paragraph, t);
            c.document->d2SetBlockAttr(id, Markoff::AttrNames::MarkerStyle,
                                        QString{}, t);
            c.cursorState->establishFocus(c.blockAnchor, 0);
            return HR::Handled;
        }

        if (c.qtPos == 0) {
            // Cursor at start of non-empty content: insert a new empty item
            // before the current one. The current item shifts to blockIndex+1.
            const Markoff::BlockId newId = Markoff::Cmd::insertListItemBefore(*c.document, id, t);
            Markoff::Cmd::renumberRunStartingAt(*c.document, newId, t);
            // Follow the original (content-bearing) item, now at blockIndex+1.
            c.cursorState->establishFocus(Markoff::BlockAnchor(id), 0);
            return HR::Handled;
        }

        if (c.qtPos == content.length()) {
            // Cursor at end: insert a new empty item after the current one.
            Markoff::BlockId newId =
                Markoff::Cmd::insertListItemAfter(*c.document, id, t);
            Markoff::Cmd::renumberRunStartingAt(*c.document, newId, t);
            c.cursorState->establishFocus(Markoff::BlockAnchor(newId), 0);
            return HR::Handled;
        }

        // Mid-content split: truncate current to prefix, set new item to suffix.
        const QByteArray prefixUtf8 = content.left(c.qtPos).toUtf8();
        const QByteArray suffixUtf8 = content.mid(c.qtPos).toUtf8();
        // Truncate current block: remove the suffix bytes.
        c.document->d2ApplyBufferEdit(id,
            static_cast<uint32_t>(prefixUtf8.size()),
            static_cast<uint32_t>(suffixUtf8.size()),
            QByteArray{}, t);
        // Insert new item after current, set its content to the suffix.
        Markoff::BlockId newId =
            Markoff::Cmd::insertListItemAfter(*c.document, id, t);
        c.document->d2ApplyBufferEdit(newId, 0, 0, suffixUtf8, t);
        Markoff::Cmd::renumberRunStartingAt(*c.document, newId, t);
        c.cursorState->establishFocus(Markoff::BlockAnchor(newId), 0);
        return HR::Handled;
    };

    // Backspace: outdent if indented and at start, else merge with previous block.
    m_handlers[BlockKind::ListItem][Qt::Key_Backspace] = [](const Ctx &c) -> HR {
        if (c.qtPos != 0) return HR::NotHandled;  // in-line: let TextEdit handle

        const Markoff::BlockId id(c.blockAnchor);
        const auto attrs = c.document->blockAttrs(id);
        const int indent = attrs.contains(Markoff::AttrNames::IndentLevel)
            ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;

        if (indent > 0) {
            UndoLog::Transaction t(c.document->d2UndoLog());
            c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                        indent - 1, t);
            Markoff::Cmd::renumberRunStartingAt(*c.document, id, t);
            c.cursorState->establishFocus(c.blockAnchor, 0);
            return HR::Handled;
        }

        if (c.blockIndex == 0) return HR::NotHandled;  // nothing before to merge into

        const int joinQtPos = c.model->recordAt(c.blockIndex - 1).text.length();
        auto result = Markoff::Cmd::backspaceMerge(*c.document, c.blockAnchor);
        if (result.mergedInto.isNull()) return HR::NotHandled;
        // backspaceMerge uses its own internal transaction; renumber in a follow-up.
        // This creates two undo entries for one Backspace, which is an acceptable
        // limitation until backspaceMerge gains a transaction parameter.
        {
            UndoLog::Transaction t(c.document->d2UndoLog());
            Markoff::Cmd::renumberRunStartingAt(*c.document, result.mergedInto, t);
        }
        c.cursorState->establishFocus(result.mergedInto, joinQtPos);
        return HR::Handled;
    };

    // Delete: merge the next block into the current one.
    m_handlers[BlockKind::ListItem][Qt::Key_Delete] = [](const Ctx &c) -> HR {
        if (c.qtPos < c.blockText.length()) return HR::NotHandled;
        if (c.blockIndex >= c.model->rowCount() - 1) return HR::NotHandled;

        Markoff::Cmd::deleteMerge(*c.document, c.blockAnchor);
        {
            UndoLog::Transaction t(c.document->d2UndoLog());
            Markoff::Cmd::renumberRunStartingAt(*c.document,
                Markoff::BlockId(c.blockAnchor), t);
        }
        c.cursorState->establishFocus(c.blockAnchor, c.qtPos);
        return HR::Handled;
    };

    // Tab: indent (or Shift+Tab: outdent). Cursor stays at same qtPos.
    m_handlers[BlockKind::ListItem][Qt::Key_Tab] = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        const auto attrs = c.document->blockAttrs(id);
        const int indent = attrs.contains(Markoff::AttrNames::IndentLevel)
            ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;

        const bool shift    = (c.modifiers & Qt::ShiftModifier) != 0;
        const int newIndent = shift ? std::max(0, indent - 1)
                                    : std::min(6, indent + 1);
        if (newIndent == indent) return HR::Handled;  // already at boundary

        if (!shift) {
            // Refuse if no preceding ListItem at indent level (the parent).
            bool parentFound = false;
            for (int k = c.blockIndex - 1; k >= 0; --k) {
                const Markoff::BlockId prevId(c.model->recordAt(k).blockAnchor);
                if (c.document->blockKind(prevId) != Markoff::BlockKind::ListItem)
                    break;
                const auto prevAttrs = c.document->blockAttrs(prevId);
                const int prevIndent = prevAttrs.contains(Markoff::AttrNames::IndentLevel)
                    ? std::get<int>(prevAttrs.value(Markoff::AttrNames::IndentLevel)) : 0;
                if (prevIndent == indent) { parentFound = true; break; }
                if (prevIndent < indent)  break;
            }
            if (!parentFound) return HR::Handled;
        }

        UndoLog::Transaction t(c.document->d2UndoLog());
        c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                    newIndent, t);
        Markoff::Cmd::renumberRunStartingAt(*c.document, id, t);
        // Also renumber the run this item left, if the previous block was a ListItem.
        if (c.blockIndex > 0) {
            const Markoff::BlockId prevId =
                c.model->recordAt(c.blockIndex - 1).blockAnchor;
            Markoff::Cmd::renumberRunStartingAt(*c.document, prevId, t);
        }
        c.cursorState->establishFocus(c.blockAnchor, c.qtPos);
        return HR::Handled;
    };

    // ---- Blockquote handlers ----
    m_handlers[BlockKind::Blockquote][Qt::Key_Return] =
    m_handlers[BlockKind::Blockquote][Qt::Key_Enter]  = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        // Empty = text is just "> " or ">"
        const bool isEmpty = (c.blockText.trimmed() == QStringLiteral(">")
                           || c.blockText.trimmed() == QStringLiteral("> "));
        if (isEmpty) {
            // Exit blockquote: demote to Paragraph AND clear the buffer.
            // Demoting alone is insufficient — KindTransition::inferBlockKind
            // re-promotes any block whose buffer starts with "> " on the
            // next onD2Changed pass, undoing the demote. Clearing the buffer
            // removes the prefix that drives the inference.
            UndoLog::Transaction t(c.document->d2UndoLog());
            c.document->d2SetBlockKind(id, Markoff::BlockKind::Paragraph, t);
            const auto bufLen = static_cast<uint32_t>(c.document->blockText(id).size());
            if (bufLen > 0)
                c.document->d2ApplyBufferEdit(id, 0, bufLen, QByteArray{}, t);
            c.cursorState->establishFocus(c.blockAnchor, 0);
            return HR::Handled;
        }
        // Insert new Blockquote after current
        UndoLog::Transaction t(c.document->d2UndoLog());
        const Markoff::BlockId newId =
            c.document->d2InsertBlock(id, Markoff::BlockKind::BlockQuote, t);
        c.cursorState->establishFocus(Markoff::BlockAnchor(newId), 0);
        return HR::Handled;
    };

    m_handlers[BlockKind::Blockquote][Qt::Key_Backspace] = [](const Ctx &c) -> HR {
        if (c.qtPos > 0) return HR::NotHandled;
        if (c.blockIndex == 0) return HR::NotHandled;
        const int joinQtPos = c.model->recordAt(c.blockIndex - 1).text.length();
        auto result = Markoff::Cmd::backspaceMerge(*c.document, c.blockAnchor);
        if (result.mergedInto.isNull()) return HR::NotHandled;
        c.cursorState->establishFocus(result.mergedInto, joinQtPos);
        return HR::Handled;
    };

    m_handlers[BlockKind::Blockquote][Qt::Key_Delete] = [](const Ctx &c) -> HR {
        if (c.qtPos < c.blockText.length()) return HR::NotHandled;
        Markoff::Cmd::deleteMerge(*c.document, c.blockAnchor);
        c.cursorState->establishFocus(c.blockAnchor,
            static_cast<int>(c.blockText.length()));
        return HR::Handled;
    };

    // Math: Backspace/Delete removes the block (same as HR/Image).
    auto mathDelete = [](const Ctx &c) -> HR {
        const Markoff::BlockId id(c.blockAnchor);
        const int targetRow = std::max(0, c.blockIndex - 1);
        UndoLog::Transaction t(c.document->d2UndoLog());
        c.document->d2RemoveBlock(id, t);
        if (c.model->rowCount() > 1) {
            const int resolveRow = std::min(targetRow, c.model->rowCount() - 2);
            c.cursorState->establishFocus(c.model->recordAt(resolveRow).blockAnchor,
                c.model->recordAt(resolveRow).text.length());
        }
        return HR::Handled;
    };
    m_handlers[BlockKind::Math][Qt::Key_Delete]    = mathDelete;
    m_handlers[BlockKind::Math][Qt::Key_Backspace] = mathDelete;
}

}  // namespace Markoff::Live
