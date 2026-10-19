// SPDX-License-Identifier: GPL-3.0-or-later
#include "InnerEditor.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPaintEvent>
#include <QTextBlock>
#include <QTextCursor>

#include <markoff/core/ClipboardCodec.h>
#include <markoff/core/Detail/FlatBlockResolve.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/core/BlockKind.h>

namespace Markoff::Source::Detail {

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

InnerEditor::InnerEditor(QWidget *parent) : QPlainTextEdit(parent) {}

QByteArray InnerEditor::selectedMarkdown() const
{
    const QTextCursor cur = textCursor();
    QString sel = cur.selectedText();
    if (sel.isEmpty())
        return {};

    // ListItem/BlockQuote markers are painted outside the text by this leaf, so
    // selectedText() is content-only for those kinds; without restoring them a
    // Copy-as-Markdown/HTML of a loaded list or quote pastes as body text
    // (ASK-M24). Unlike styled, this leaf's QPlainTextEdit text is the whole
    // flat view — one document block may span several lines — so a line starts
    // a block exactly when its separator-view byte resolves to byte 0 of that
    // block.
    if (m_document) {
        const QString flatText = toPlainText();
        const int selStart = qMin(cur.anchor(), cur.position());
        const QList<qsizetype> lineStarts = [&] {
            QList<qsizetype> starts;
            qsizetype at = 0;
            starts.append(at);
            for (qsizetype i = 0; i < sel.size(); ++i) {
                if (sel.at(i) == QChar(0x2029)) {
                    at = i + 1;
                    starts.append(at);
                }
            }
            return starts;
        }();

        sel = Markoff::ClipboardCodec::restoreBlockMarkers(
            sel, [&](qsizetype i) -> QByteArray {
                if (i < 0 || i >= lineStarts.size())
                    return {};
                const qsizetype lineStart = lineStarts[i];
                if (sel.size() > lineStart && sel.at(lineStart) == QChar(0x2029))
                    return {};  // empty line: no block begins here
                const quint32 sepByte =
                    Markoff::SourceTextDocumentBinding::qtPosToByteOffset(
                        flatText, selStart + int(lineStart));
                const auto hit = Markoff::Detail::findBlockAtSepByte(
                    m_document.data(), sepByte, /*biasForward=*/true);
                if (!hit || hit->byteInBlock != 0)
                    return {};
                const Markoff::BlockId id = hit->blockId;
                if (m_document->blockKind(id) == Markoff::BlockKind::ListItem)
                    return m_document->listItemDisplayMarker(id);
                if (m_document->blockKind(id) == Markoff::BlockKind::BlockQuote)
                    return m_document->blockQuoteDisplayMarker(id);
                return {};
            });
    }

    sel.replace(QChar(0x2029), QLatin1Char('\n'));
    return sel.toUtf8();
}

void InnerEditor::copyWithFlavor(Markoff::ClipboardCodec::Flavor flavor)
{
    const QByteArray md = selectedMarkdown();
    if (md.isEmpty())
        return;
    QGuiApplication::clipboard()->setMimeData(
        Markoff::ClipboardCodec::mimeFromMarkdown(md, {}, flavor));
}

void InnerEditor::pasteWithMode(Markoff::ClipboardCodec::PasteMode mode)
{
    if (isReadOnly())
        return;
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return;
    const QByteArray md = Markoff::ClipboardCodec::markdownFromMime(mime, mode);
    if (md.isEmpty())
        return;
    textCursor().insertText(QString::fromUtf8(md));
}

void InnerEditor::paintEvent(QPaintEvent *event)
{
    // Base class paints the document text first — each ListItem block's
    // QTextBlockFormat left margin (set by Editor::applyListItemMarkerDecorations)
    // already reserves the gap this method draws into, so text never overlaps
    // the marker.
    QPlainTextEdit::paintEvent(event);

    if (m_listItemMarkers.isEmpty()) return;

    QPainter p(viewport());
    p.setFont(font());
    p.setPen(palette().text().color());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const auto it = m_listItemMarkers.constFind(blockNumber);
            if (it != m_listItemMarkers.constEnd() && !it.value().isEmpty()) {
                const QTextBlockFormat bf = block.blockFormat();
                p.drawText(QRectF(0, top, bf.leftMargin(), bottom - top),
                           Qt::AlignLeft | Qt::AlignVCenter, it.value());
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++blockNumber;
    }
}

QMimeData *InnerEditor::createMimeDataFromSelection() const
{
    const QByteArray md = selectedMarkdown();
    if (md.isEmpty())
        return nullptr;
    return Markoff::ClipboardCodec::mimeFromMarkdown(
        md, {}, Markoff::ClipboardCodec::Flavor::All);
}

bool InnerEditor::canInsertFromMimeData(const QMimeData *source) const
{
    return mimeOffersPaste(source);
}

void InnerEditor::insertFromMimeData(const QMimeData *source)
{
    if (isReadOnly() || !source)
        return;
    const QByteArray md = Markoff::ClipboardCodec::markdownFromMime(
        source, Markoff::ClipboardCodec::PasteMode::Smart);
    if (md.isEmpty())
        return;
    textCursor().insertText(QString::fromUtf8(md));
}

void InnerEditor::keyPressEvent(QKeyEvent *e)
{
    // Obsidian-faithful Paste as Plain (Ctrl+Shift+V).
    if (e->key() == Qt::Key_V
        && (e->modifiers() & Qt::ControlModifier)
        && (e->modifiers() & Qt::ShiftModifier)
        && !(e->modifiers() & Qt::AltModifier)) {
        pasteWithMode(Markoff::ClipboardCodec::PasteMode::Plain);
        e->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

} // namespace Markoff::Source::Detail
