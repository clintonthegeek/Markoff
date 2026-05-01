// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QStringList>
#include <qqmlintegration.h>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>

namespace Markoff::View::Qml {

/// Session-canonical cross-block selection. Replaces `LiveSelectionModel`.
///
/// Subscribes to `Session::primarySelectionChanged` and projects the
/// foundation-level `(anchor, active)` TextAnchors to per-delegate
/// `(startQChar, endQChar)` ranges via foundation translation APIs.
///
/// Mouse-drag write path: `begin` / `extend` / `clear` translate a QML
/// block-index + UTF-16 QChar offset to a TextAnchor via
/// `MarkoffDocument::textAnchorAt(BlockAnchor, byteOffset, rightBias)`,
/// then call `Session::setPrimarySelection`.
///
/// QML consumer contract:
///   - `rangeForBlock` returns QPoint(-1,-1) for unselected blocks.
///   - For selected blocks the y component MAY equal INT32_MAX as a
///     "to end of block" sentinel. CONSUMERS MUST CLAMP via
///     min(y, textEdit.length) before calling TextEdit.select(start, end).
class LiveSelectionView : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

public:
    explicit LiveSelectionView(QObject *parent = nullptr);

    void setDocument(Markoff::MarkoffDocument *doc);
    void setSession(Markoff::Session *session);

    bool hasSelection() const;

    /// Returns the document (non-owning). May be nullptr before setDocument().
    Markoff::MarkoffDocument *document() const { return m_document; }

    /// Returns the canonical selection byte range [min, max) in UTF-8 document bytes.
    /// If selection is degenerate (cursor), min == max == cursor position.
    /// Returns {0, 0} if no document is set.
    QPair<quint32, quint32> selectionByteRange() const;

    /// Returns the selection range (in UTF-16 QChar units) for the given block.
    /// Returns QPoint(-1,-1) if the block is not in the selection.
    /// The y component may be INT32_MAX as a "to end of block" sentinel.
    Q_INVOKABLE QPoint rangeForBlock(int blockIndex) const;

    /// Copies the selected text from the given block-text list to the clipboard.
    Q_INVOKABLE void copySelectionToClipboard(const QStringList &blockTexts) const;

    // --- Write path (mouse drag) ---

    /// Begin a new selection at the given block-index + UTF-16 QChar offset.
    Q_INVOKABLE void begin(int blockIndex, int qtOffset);

    /// Extend the current selection to the given block-index + UTF-16 QChar offset.
    Q_INVOKABLE void extend(int blockIndex, int qtOffset);

    /// Clear the selection (sets degenerate selection at document start).
    Q_INVOKABLE void clear();

    // --- Model update hooks ---

    /// Called by LiveListModelBinding after an AST diff. Clears the selection
    /// if either end of the selection falls inside a deleted block.
    void notifyBlocksRemoved(const QList<Markoff::BlockAnchor> &deletedBlockAnchors);

Q_SIGNALS:
    void selectionChanged();

private Q_SLOTS:
    void onPrimarySelectionChanged(const Markoff::Selection &sel);

private:
    /// Translate (blockIndex, qtOffset) to a TextAnchor. Returns a default
    /// TextAnchor{} if the block cannot be resolved.
    Markoff::TextAnchor anchorForBlockOffset(int blockIndex, int qtOffset,
                                             bool rightBias) const;

    /// Return the within-block QChar offset for a TextAnchor, or -1 if the
    /// anchor is outside the block.
    int qtOffsetInBlock(const Markoff::BlockAnchor &block,
                        const Markoff::TextAnchor &ta) const;

    Markoff::MarkoffDocument *m_document = nullptr;
    Markoff::Session         *m_session  = nullptr;
    Markoff::Selection        m_selection;
    bool                      m_applyingSessionSelection = false;
};

}  // namespace Markoff::View::Qml
