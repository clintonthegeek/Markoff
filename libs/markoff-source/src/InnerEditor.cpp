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
    QString sel = textCursor().selectedText();
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
