// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveSelectionView.h>

#include <markoff-foundation/SourceTextDocumentBinding.h>

#include <QClipboard>
#include <QGuiApplication>

namespace Markoff::View::Qml {

LiveSelectionView::LiveSelectionView(QObject *parent) : QObject(parent) {}

void LiveSelectionView::setDocument(Markoff::MarkoffDocument *doc)
{
    m_document = doc;
}

void LiveSelectionView::setSession(Markoff::Session *session)
{
    if (m_session == session) return;
    if (m_session) {
        QObject::disconnect(m_session, &Markoff::Session::primarySelectionChanged,
                            this, &LiveSelectionView::onPrimarySelectionChanged);
    }
    m_session = session;
    if (m_session) {
        QObject::connect(m_session, &Markoff::Session::primarySelectionChanged,
                         this, &LiveSelectionView::onPrimarySelectionChanged);
        m_selection = m_session->primarySelection();
    } else {
        m_selection = Markoff::Selection{};
    }
    Q_EMIT selectionChanged();
}

void LiveSelectionView::onPrimarySelectionChanged(const Markoff::Selection &sel)
{
    if (m_applyingSessionSelection) return;
    m_selection = sel;
    Q_EMIT selectionChanged();
}

bool LiveSelectionView::hasSelection() const
{
    if (!m_document) return false;
    const quint32 anchorByte = m_document->resolveTextAnchor(m_selection.anchor);
    const quint32 activeByte = m_document->resolveTextAnchor(m_selection.active);
    return anchorByte != activeByte;
}

QPoint LiveSelectionView::rangeForBlock(int blockIndex) const
{
    if (!m_document) return QPoint(-1, -1);
    if (!hasSelection()) return QPoint(-1, -1);

    const auto blockAnchorOpt = m_document->blockAnchorAt(blockIndex);
    if (!blockAnchorOpt.has_value()) return QPoint(-1, -1);
    const Markoff::BlockAnchor &ba = blockAnchorOpt.value();

    const auto rangeOpt = m_document->blockByteRange(ba);
    if (!rangeOpt.has_value()) return QPoint(-1, -1);
    const quint32 blockStart = rangeOpt->first;
    const quint32 blockEnd   = rangeOpt->second;

    const quint32 anchorByte = m_document->resolveTextAnchor(m_selection.anchor);
    const quint32 activeByte = m_document->resolveTextAnchor(m_selection.active);
    const quint32 selStart   = std::min(anchorByte, activeByte);
    const quint32 selEnd     = std::max(anchorByte, activeByte);

    // Block is entirely outside the selection.
    // Use strict-less for the right boundary so that a drag that ends exactly at
    // a block's first byte (selEnd == blockStart) still registers as touching
    // the block — this matches non-text delegates (image/hr) where positionAt
    // always returns 0 and the active anchor sits at the block's start byte.
    if (selEnd < blockStart || selStart >= blockEnd) return QPoint(-1, -1);

    const QByteArray docUtf8 = m_document->toMarkdownUtf8();

    // within-block start QChar position
    int startQChar;
    if (selStart <= blockStart) {
        startQChar = 0;
    } else {
        const quint32 byteInBlock = selStart - blockStart;
        const QByteArray blockUtf8 = docUtf8.mid(static_cast<int>(blockStart),
                                                  static_cast<int>(blockEnd - blockStart));
        startQChar = Markoff::SourceTextDocumentBinding::byteOffsetToQtPos(blockUtf8, byteInBlock);
    }

    // within-block end QChar position (INT32_MAX = "to end of block")
    int endQChar;
    if (selEnd >= blockEnd) {
        endQChar = INT32_MAX;
    } else {
        const quint32 byteInBlock = selEnd - blockStart;
        const QByteArray blockUtf8 = docUtf8.mid(static_cast<int>(blockStart),
                                                  static_cast<int>(blockEnd - blockStart));
        endQChar = Markoff::SourceTextDocumentBinding::byteOffsetToQtPos(blockUtf8, byteInBlock);
    }

    return QPoint(startQChar, endQChar);
}

void LiveSelectionView::copySelectionToClipboard(const QStringList &blockTexts) const
{
    if (!hasSelection()) return;
    if (!m_document) return;

    const quint32 anchorByte = m_document->resolveTextAnchor(m_selection.anchor);
    const quint32 activeByte = m_document->resolveTextAnchor(m_selection.active);
    const quint32 selStart   = std::min(anchorByte, activeByte);
    const quint32 selEnd     = std::max(anchorByte, activeByte);

    // Find the first and last block indices in the selection.
    int firstBlock = -1, lastBlock = -1;
    for (int i = 0; i < blockTexts.size(); ++i) {
        const auto blockAnchorOpt = m_document->blockAnchorAt(i);
        if (!blockAnchorOpt.has_value()) continue;
        const auto rangeOpt = m_document->blockByteRange(blockAnchorOpt.value());
        if (!rangeOpt.has_value()) continue;
        if (rangeOpt->second <= selStart || rangeOpt->first >= selEnd) continue;
        if (firstBlock < 0) firstBlock = i;
        lastBlock = i;
    }
    if (firstBlock < 0) return;

    const QByteArray docUtf8 = m_document->toMarkdownUtf8();
    QString out;

    for (int i = firstBlock; i <= lastBlock; ++i) {
        if (i >= blockTexts.size()) break;
        const auto blockAnchorOpt = m_document->blockAnchorAt(i);
        if (!blockAnchorOpt.has_value()) continue;
        const auto rangeOpt = m_document->blockByteRange(blockAnchorOpt.value());
        if (!rangeOpt.has_value()) continue;
        const quint32 bs = rangeOpt->first;
        const quint32 be = rangeOpt->second;
        const QByteArray blockUtf8 = docUtf8.mid(static_cast<int>(bs),
                                                   static_cast<int>(be - bs));

        const int startQChar = (selStart <= bs)
            ? 0
            : Markoff::SourceTextDocumentBinding::byteOffsetToQtPos(blockUtf8, selStart - bs);
        const int endQChar = (selEnd >= be)
            ? blockTexts.at(i).size()
            : Markoff::SourceTextDocumentBinding::byteOffsetToQtPos(blockUtf8, selEnd - bs);

        if (i > firstBlock) out += QChar('\n');
        out += blockTexts.at(i).mid(startQChar, endQChar - startQChar);
    }

    if (!out.isEmpty())
        QGuiApplication::clipboard()->setText(out);
}

void LiveSelectionView::begin(int blockIndex, int qtOffset)
{
    const Markoff::TextAnchor ta = anchorForBlockOffset(blockIndex, qtOffset, false);
    if (!m_session) return;

    m_applyingSessionSelection = true;
    Markoff::Selection sel;
    sel.anchor = ta;
    sel.active = ta;
    sel.kind   = Markoff::Selection::Kind::Primary;
    m_session->setPrimarySelection(sel);
    m_selection = sel;
    m_applyingSessionSelection = false;
    Q_EMIT selectionChanged();
}

void LiveSelectionView::extend(int blockIndex, int qtOffset)
{
    if (!m_session || !m_document) return;

    // If no session yet, treat as begin.
    const quint32 anchorByte = m_document->resolveTextAnchor(m_selection.anchor);
    const quint32 activeByte = m_document->resolveTextAnchor(m_selection.active);
    const Markoff::TextAnchor newActive = anchorForBlockOffset(blockIndex, qtOffset, true);
    const quint32 newActiveByte = m_document->resolveTextAnchor(newActive);
    if (anchorByte == activeByte && newActiveByte == activeByte) return;  // no change
    if (newActive == m_selection.active) return;

    m_applyingSessionSelection = true;
    Markoff::Selection sel;
    sel.anchor = m_selection.anchor;
    sel.active = newActive;
    sel.kind   = Markoff::Selection::Kind::Primary;
    m_session->setPrimarySelection(sel);
    m_selection = sel;
    m_applyingSessionSelection = false;
    Q_EMIT selectionChanged();
}

void LiveSelectionView::clear()
{
    if (!m_session || !m_document) return;
    if (!hasSelection()) return;

    m_applyingSessionSelection = true;
    // Collapse to a degenerate selection at the document start.
    Markoff::Selection sel;
    sel.anchor = m_document->textAnchorAt(0, false);
    sel.active = sel.anchor;
    sel.kind   = Markoff::Selection::Kind::Primary;
    m_session->setPrimarySelection(sel);
    m_selection = sel;
    m_applyingSessionSelection = false;
    Q_EMIT selectionChanged();
}

Markoff::TextAnchor LiveSelectionView::anchorForBlockOffset(int blockIndex,
                                                             int qtOffset,
                                                             bool rightBias) const
{
    if (!m_document) return Markoff::TextAnchor{};

    const auto blockAnchorOpt = m_document->blockAnchorAt(blockIndex);
    if (!blockAnchorOpt.has_value()) return Markoff::TextAnchor{};
    const Markoff::BlockAnchor &ba = blockAnchorOpt.value();

    const auto rangeOpt = m_document->blockByteRange(ba);
    if (!rangeOpt.has_value()) return Markoff::TextAnchor{};
    const quint32 blockStart = rangeOpt->first;
    const quint32 blockEnd   = rangeOpt->second;

    // Convert qtOffset (UTF-16 QChar) → UTF-8 byte offset within block.
    const QByteArray docUtf8  = m_document->toMarkdownUtf8();
    const QByteArray blockUtf8 = docUtf8.mid(static_cast<int>(blockStart),
                                               static_cast<int>(blockEnd - blockStart));
    const QString blockText   = QString::fromUtf8(blockUtf8);
    const quint32 byteInBlock = Markoff::SourceTextDocumentBinding::qtPosToByteOffset(
        blockText, qtOffset);

    return m_document->textAnchorAt(ba, static_cast<int>(byteInBlock), rightBias);
}

void LiveSelectionView::notifyBlocksRemoved(
    const QList<Markoff::BlockAnchor> &deletedBlockAnchors)
{
    if (!m_document || !hasSelection()) return;

    const auto anchorBlock = m_document->blockAt(m_selection.anchor);
    const auto activeBlock = m_document->blockAt(m_selection.active);

    for (const Markoff::BlockAnchor &deleted : deletedBlockAnchors) {
        bool anchorDeleted = anchorBlock.has_value() && anchorBlock.value() == deleted;
        bool activeDeleted = activeBlock.has_value() && activeBlock.value() == deleted;
        if (anchorDeleted || activeDeleted) {
            clear();
            return;
        }
    }
}

int LiveSelectionView::qtOffsetInBlock(const Markoff::BlockAnchor &block,
                                        const Markoff::TextAnchor &ta) const
{
    if (!m_document) return -1;
    const int byteOff = m_document->offsetInBlock(block, ta);
    const auto rangeOpt = m_document->blockByteRange(block);
    if (!rangeOpt.has_value()) return -1;
    const QByteArray docUtf8 = m_document->toMarkdownUtf8();
    const QByteArray blockUtf8 = docUtf8.mid(static_cast<int>(rangeOpt->first),
                                               static_cast<int>(rangeOpt->second - rangeOpt->first));
    return Markoff::SourceTextDocumentBinding::byteOffsetToQtPos(blockUtf8,
                                                                   static_cast<quint32>(byteOff));
}

}  // namespace Markoff::View::Qml
