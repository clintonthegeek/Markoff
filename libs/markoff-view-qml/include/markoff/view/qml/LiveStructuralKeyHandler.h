// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <qqmlintegration.h>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::View::Qml {

class LiveBlockModel;
class LiveProjectionLayer;

/// Handles structural key events that cross block boundaries.
///
/// Backspace at offset 0 merges with the previous block by deleting the
/// inter-block newline separator. Delete at end-of-block merges with the
/// next block. Enter inserts a paragraph break ("\n\n"); inside a code_block
/// it passes through so TextEdit inserts a literal newline.
class LiveStructuralKeyHandler : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::MarkoffDocument *document
               READ document
               WRITE setDocument
               NOTIFY documentChanged)
    Q_PROPERTY(Markoff::View::Qml::LiveBlockModel *model
               READ model
               WRITE setModel
               NOTIFY modelChanged)
    Q_PROPERTY(Markoff::View::Qml::LiveProjectionLayer *projectionLayer
               READ projectionLayer
               WRITE setProjectionLayer
               NOTIFY projectionLayerChanged)

public:
    explicit LiveStructuralKeyHandler(QObject *parent = nullptr);

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *doc);

    LiveBlockModel *model() const;
    void setModel(LiveBlockModel *m);

    LiveProjectionLayer *projectionLayer() const;
    void setProjectionLayer(LiveProjectionLayer *layer);

    /// Backspace inside an empty hole row at qtPos 0: drop the hole, return
    /// true. The QML layer then routes focus back to the previous row.
    /// (Spec §3.3 backspace edge case + T22.)
    Q_INVOKABLE bool tryHandleHoleBackspace(int viewRow, int qtPos,
                                            const QString &blockText);

    /// Try to handle a structural key event.
    ///
    /// Returns true if the key was consumed (caller should set event.accepted = true).
    /// Returns false for all keys not listed below, and for "inside code_block" Enter/Tab.
    ///
    /// rowAnchor    — BlockAnchor for the focused block
    /// blockIndex   — index of the focused block in the model
    /// qtPos        — UTF-16 cursor position within the block's text
    /// selectionEmpty — true when there is no selection
    /// blockText    — the block's current text (used for qtPos→byteOffset)
    Q_INVOKABLE bool tryHandle(int key, int modifiers,
                               const Markoff::BlockAnchor &rowAnchor,
                               int blockIndex,
                               int qtPos,
                               bool selectionEmpty,
                               const QString &blockText);

    /// Insert the very first character into an empty document.
    ///
    /// Called by LiveView.qml when the document has 0 rows and the user presses
    /// a printable key. Creates a paragraph block containing \a text.
    Q_INVOKABLE void insertFirstCharacter(const QString &text);

Q_SIGNALS:
    void documentChanged();
    void modelChanged();
    void projectionLayerChanged();
    /// Emitted when a paragraph hole has been created. The QML layer wires
    /// focus into the new hole row at cursorPosition 0.
    void blockHoleCreated(quint64 holeId, int viewRow);

private:
    Markoff::MarkoffDocument *m_document = nullptr;
    LiveBlockModel           *m_model    = nullptr;
    LiveProjectionLayer      *m_layer    = nullptr;
};

}  // namespace Markoff::View::Qml
