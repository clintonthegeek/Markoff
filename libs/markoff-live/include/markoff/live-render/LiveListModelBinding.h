// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/BlockHitTester.h>
#include <markoff/live-render/LiveSelectionView.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>

#include <QObject>
#include <memory>
#include <qqmlintegration.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockAnchor.h>

namespace Markoff::Live {

class MARKOFF_LIVE_RENDER_EXPORT LiveListModelBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::MarkoffDocument *document
               READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Live::LiveBlockModel *model
               READ model CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveCursorState *cursorState
               READ cursorState CONSTANT)
    Q_PROPERTY(Markoff::Live::BlockHitTester *hitTester
               READ hitTester CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveSelectionView *selectionView
               READ selectionView CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveStructuralKeyHandler *structuralKeyHandler
               READ structuralKeyHandler CONSTANT)

public:
    explicit LiveListModelBinding(QObject *parent = nullptr);
    ~LiveListModelBinding() override;

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *doc);

    LiveBlockModel           *model()               const;
    LiveCursorState          *cursorState()         const;
    BlockHitTester           *hitTester()           const;
    LiveSelectionView        *selectionView()       const;
    LiveStructuralKeyHandler *structuralKeyHandler() const;
    const BlockKindRegistry  *registry()            const;

    /// True for the synchronous duration of `applyOps` while D2 CRDT change
    /// notification is mutating model rows. LiveEditBinding queries this
    /// inside its `contentsChange` slot to suppress the synchronous
    /// QML-binding echo.
    bool applyingModelUpdate() const;

Q_SIGNALS:
    void documentChanged();

    /// Emitted after applyOps when blocks were structurally inserted.
    /// `first` and `last` are the new model row indices (inclusive).
    void structuralRowsInserted(int first, int last);

    /// Emitted after applyOps when a block was structurally removed.
    /// `row` is the row index that was removed.
    void structuralRowRemoved(int row);

private:
    void onD2Changed();

    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::Live
