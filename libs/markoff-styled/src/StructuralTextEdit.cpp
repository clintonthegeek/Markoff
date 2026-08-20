// SPDX-License-Identifier: GPL-3.0-or-later
#include "StructuralTextEdit.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QTextCursor>
#include <QTextTable>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace Markoff::Styled {

namespace {

bool mimeOffersPaste(const QMimeData *mime)
{
    if (!mime)
        return false;
    using namespace Markoff::ClipboardCodec;
    return mime->hasText() || mime->hasHtml()
        || mime->hasFormat(QString::fromUtf8(kMarkdownMime))
        || mime->hasFormat(QString::fromUtf8(kRtfMime))
        || mime->hasFormat(QStringLiteral("application/rtf"))
        || mime->hasFormat(QString::fromUtf8(kBlocksMime));
}

}  // namespace

StructuralTextEdit::StructuralTextEdit(QWidget *parent) : QTextEdit(parent) {}

QByteArray StructuralTextEdit::selectedMarkdown() const
{
    // For non-table documents, QTextDocument content == widgetFlatView()
    // (markdown with delimiters). Table frames diverge; v1 still exports the
    // visible selection text rather than themed Qt HTML.
    const QTextCursor cur = textCursor();
    QString sel = cur.selectedText();
    if (sel.isEmpty())
        return {};

    // Restore ListItem/BlockQuote markers on copy. StyleApplier renders
    // bullets via native QTextList decoration and quote depth via a
    // QTextBlockFormat left-margin (both non-text), so selectedText() is
    // content-only for these two kinds — without this, Copy as HTML/RTF of
    // a loaded quote/list re-parses bare content as a plain paragraph and
    // pastes into e.g. LibreOffice as Body Text, not a Block Quote/List
    // (same bug class, mirrors the identical fix in the canvas leaf's
    // View::selectedText()). Only the FIRST line of a multi-line selection
    // can be a byte-offset-mid-block slice; "WP unification" (each D2
    // block == exactly one QTextBlock, in order) makes every OTHER line
    // start exactly at byte 0 of its own block by construction, so
    // QTextBlock::blockNumber() indexes iterateBlocks() directly.
    if (m_binding && m_binding->markoffDocument()) {
        auto *doc = m_binding->markoffDocument();
        const auto blocks = doc->iterateBlocks();
        QTextCursor startCur(cur);
        startCur.setPosition(qMin(cur.anchor(), cur.position()));
        const int startBlockNum = startCur.block().blockNumber();
        const bool firstLineIsBlockStart = (startCur.positionInBlock() == 0);

        QStringList lines = sel.split(QChar(0x2029));
        for (int i = 0; i < lines.size(); ++i) {
            if (i == 0 && !firstLineIsBlockStart)
                continue;
            const int blockIdx = startBlockNum + i;
            if (blockIdx < 0 || blockIdx >= int(blocks.size()))
                continue;
            const Markoff::BlockId id = blocks[size_t(blockIdx)];
            QByteArray marker;
            if (doc->blockKind(id) == Markoff::BlockKind::ListItem)
                marker = doc->listItemDisplayMarker(id);
            else if (doc->blockKind(id) == Markoff::BlockKind::BlockQuote)
                marker = doc->blockQuoteDisplayMarker(id);
            if (!marker.isEmpty())
                lines[i] = QString::fromUtf8(marker) + lines[i];
        }
        sel = lines.join(QChar(0x2029));
    }

    sel.replace(QChar(0x2029), QLatin1Char('\n'));
    return sel.toUtf8();
}

void StructuralTextEdit::copyWithFlavor(Markoff::ClipboardCodec::Flavor flavor)
{
    const QByteArray md = selectedMarkdown();
    if (md.isEmpty())
        return;
    QGuiApplication::clipboard()->setMimeData(
        Markoff::ClipboardCodec::mimeFromMarkdown(md, {}, flavor));
}

void StructuralTextEdit::pasteWithMode(Markoff::ClipboardCodec::PasteMode mode)
{
    if (isReadOnly())
        return;
    // Tables are read-only in Styled; don't paste into a cell frame.
    if (textCursor().currentTable() != nullptr)
        return;
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return;
    const QByteArray md = Markoff::ClipboardCodec::markdownFromMime(mime, mode);
    if (md.isEmpty())
        return;
    textCursor().insertText(QString::fromUtf8(md));
}

QMimeData *StructuralTextEdit::createMimeDataFromSelection() const
{
    const QByteArray md = selectedMarkdown();
    if (md.isEmpty())
        return nullptr;
    return Markoff::ClipboardCodec::mimeFromMarkdown(
        md, {}, Markoff::ClipboardCodec::Flavor::All);
}

bool StructuralTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return mimeOffersPaste(source);
}

void StructuralTextEdit::insertFromMimeData(const QMimeData *source)
{
    if (isReadOnly() || !source)
        return;
    if (textCursor().currentTable() != nullptr)
        return;
    const QByteArray md = Markoff::ClipboardCodec::markdownFromMime(
        source, Markoff::ClipboardCodec::PasteMode::Smart);
    if (md.isEmpty())
        return;
    textCursor().insertText(QString::fromUtf8(md));
}

void StructuralTextEdit::keyPressEvent(QKeyEvent *e) {
    // Read-only tables: a table block is rendered as an opaque QTextTable frame.
    // The styled view does not edit tables in place — edit them in Source mode.
    // When the caret is inside a frame, let navigation/selection keys through
    // (so the caret can move across and out of the table) but swallow anything
    // that would mutate the frame's text.
    if (textCursor().currentTable() != nullptr) {
        switch (e->key()) {
            case Qt::Key_Left:    case Qt::Key_Right:
            case Qt::Key_Up:      case Qt::Key_Down:
            case Qt::Key_Home:    case Qt::Key_End:
            case Qt::Key_PageUp:  case Qt::Key_PageDown:
            case Qt::Key_Tab:     case Qt::Key_Backtab:
                // Navigation (incl. Tab cell-to-cell). Native handling moves the
                // caret without editing.
                QTextEdit::keyPressEvent(e);
                return;
            default:
                // Allow copy/select-all (read affordances); swallow the rest.
                // Ctrl+C uses createMimeDataFromSelection (codec path).
                if ((e->modifiers() & Qt::ControlModifier)
                    && (e->key() == Qt::Key_C || e->key() == Qt::Key_A)) {
                    QTextEdit::keyPressEvent(e);
                    return;
                }
                e->accept();
                return;
        }
    }

    // Obsidian-faithful Paste as Plain (Ctrl+Shift+V).
    if (e->key() == Qt::Key_V
        && (e->modifiers() & Qt::ControlModifier)
        && (e->modifiers() & Qt::ShiftModifier)
        && !(e->modifiers() & Qt::AltModifier)) {
        pasteWithMode(Markoff::ClipboardCodec::PasteMode::Plain);
        e->accept();
        return;
    }

    if (m_binding) {
        const int key = e->key();
        const auto mods = e->modifiers();

        // Undo/redo: styled disables QTextDocument's own undo stack, so route
        // to the foundation's D2 undo.
        if ((mods & Qt::ControlModifier) && !(mods & Qt::AltModifier)) {
            Markoff::MarkoffDocument *doc = m_binding->markoffDocument();
            // If doc is null the binding isn't fully wired yet; the undo/redo
            // branches below are skipped and the chord falls through to native,
            // which is a no-op (the QTextDocument's own undo stack is disabled).
            if (doc && key == Qt::Key_Z && !(mods & Qt::ShiftModifier)) {
                doc->undoD2(); e->accept(); return;
            }
            if (doc && (key == Qt::Key_Y
                        || (key == Qt::Key_Z && (mods & Qt::ShiftModifier)))) {
                doc->redoD2(); e->accept(); return;
            }
        }

        // Structural keys → forward to the binding before native editing runs.
        const bool structural =
            key == Qt::Key_Return || key == Qt::Key_Enter
            || key == Qt::Key_Backspace || key == Qt::Key_Delete
            || key == Qt::Key_Tab || key == Qt::Key_Backtab;
        if (structural) {
            const QTextCursor c = textCursor();
            if (m_binding->handleStructuralKey(
                    key, static_cast<int>(mods), c.position(), c.anchor())) {
                e->accept();
                return;
            }
        }
    }
    QTextEdit::keyPressEvent(e);
}

}  // namespace Markoff::Styled
