// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/core/Selection.h>

#include <QObject>
#include <QPoint>
#include <QStringList>
#include <qqmlintegration.h>

namespace Markoff {
class MarkoffDocument;
class Session;
}

namespace Markoff::Live {

class LiveBlockModel;

/// Cross-block selection for the live render view.
///
/// State: anchor (blockIndex + qtPos) + active (blockIndex + qtPos).
/// Write path from QML: begin(block, qtPos) / extend(block, qtPos) / clear().
/// Read path for rendering: rangeForBlock(n) → QPoint(start, end) or (-1,-1).
/// The `end` component may be INT32_MAX ("to end of block") — consumers must
/// clamp via Math.min(r.y, textEdit.length) before calling TextEdit.select.
///
/// After every begin/extend the selection is synced to Session::setPrimarySelection
/// (converting qtPos → TextAnchor via MarkoffDocument::textAnchorAt) so the
/// CRDT layer has accurate cursor state.
class MARKOFF_LIVE_EXPORT LiveSelectionView : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveSelectionView is provided by LiveListModelBinding")

    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

public:
    explicit LiveSelectionView(QObject *parent = nullptr);

    void setDocument(Markoff::MarkoffDocument *doc);
    Q_INVOKABLE void setSession(Markoff::Session *session);
    void setModel(const LiveBlockModel *model);

    bool hasSelection() const;

    Q_INVOKABLE void begin(int blockIndex, int qtPos);
    Q_INVOKABLE void extend(int blockIndex, int qtPos);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deleteSelection();

    /// Returns QPoint(start, end) for the block, or QPoint(-1,-1) if untouched.
    /// end may be INT32_MAX — consumers must clamp to textEdit.length.
    Q_INVOKABLE QPoint rangeForBlock(int blockIndex) const;

    /// Copy the current selection to the system clipboard. Reads block
    /// texts directly from the bound `LiveBlockModel` — DO NOT walk
    /// ListView delegates from QML: ListView only realises the visible
    /// window, so off-screen rows in a cross-block selection would
    /// contribute empty strings.
    Q_INVOKABLE void copyToClipboard() const;

    // Accessors used by LiveClipboardController to compute paste byte offsets,
    // by LiveNavigationController to detect cross-block extension start, and
    // by QML delegates' selection-sync path.
    Q_INVOKABLE int anchorBlock() const { return m_anchorBlock; }
    Q_INVOKABLE int anchorQtPos() const { return m_anchorQtPos; }
    Q_INVOKABLE int activeBlock() const { return m_activeBlock; }
    Q_INVOKABLE int activeQtPos() const { return m_activeQtPos; }

Q_SIGNALS:
    void selectionChanged();

private Q_SLOTS:
    void onSessionPrimarySelectionChanged(const Markoff::Selection &sel);

private:
    void normalized(int &fb, int &fo, int &lb, int &lo) const;
    void syncToSession();

    bool m_applyingSessionSelection = false;

    int m_anchorBlock = -1, m_anchorQtPos = -1;
    int m_activeBlock = -1, m_activeQtPos = -1;

    Markoff::MarkoffDocument *m_document = nullptr;
    Markoff::Session         *m_session  = nullptr;
    const LiveBlockModel     *m_model    = nullptr;
};

}  // namespace Markoff::Live
