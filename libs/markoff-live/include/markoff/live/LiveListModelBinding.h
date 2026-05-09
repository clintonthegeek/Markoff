// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockHitTester.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/LiveStructuralKeyHandler.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/LiveClipboardController.h>
#include <markoff/live/LiveActionController.h>
#include <markoff/live/LiveFormatController.h>

#include <QObject>
#include <QAbstractListModel>
#include <memory>
#include <qqmlintegration.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff/core/Session.h>
#include <markoff/core/Theme.h>

namespace Markoff::Live {

class MARKOFF_LIVE_EXPORT LiveListModelBinding : public QObject {
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
    Q_PROPERTY(Markoff::Live::LiveNavigationController *navigationController
               READ navigationController CONSTANT)
    Q_PROPERTY(QAbstractListModel *remoteCursorsModel
               READ remoteCursorsModel CONSTANT)
    Q_PROPERTY(const Markoff::Theme *theme READ theme WRITE setTheme NOTIFY themeChanged)

    Q_PROPERTY(Markoff::Live::LiveClipboardController *clipboardController
               READ clipboardController CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveActionController *actionController
               READ actionController CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveFormatController *formatController
               READ formatController CONSTANT)

public:
    enum Capability {
        NoCapabilities  = 0,
        Clipboard       = 1 << 0,
        Format          = 1 << 1,
        Actions         = 1 << 2,
        Session         = 1 << 3,
        AllCapabilities = Clipboard | Format | Actions | Session,
    };
    Q_DECLARE_FLAGS(Capabilities, Capability)
    Q_FLAG(Capabilities)

    explicit LiveListModelBinding(QObject *parent = nullptr);
    explicit LiveListModelBinding(Capabilities caps, QObject *parent = nullptr);
    ~LiveListModelBinding() override;

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *doc);

    /// Bind a Session so that Session::primarySelectionChanged propagates into
    /// the SelectionView. Pass nullptr to detach. The binding does NOT take
    /// ownership of the session (it is owned by the MarkoffDocument).
    void setSession(Markoff::Session *session);

    LiveBlockModel           *model()               const;
    LiveCursorState          *cursorState()         const;
    BlockHitTester           *hitTester()           const;
    LiveSelectionView        *selectionView()       const;
    LiveStructuralKeyHandler *structuralKeyHandler() const;
    LiveNavigationController *navigationController() const;
    const BlockKindRegistry  *registry()            const;
    QAbstractListModel       *remoteCursorsModel()  const;

    LiveClipboardController *clipboardController() const;
    LiveActionController    *actionController()    const;
    LiveFormatController    *formatController()    const;

    const Markoff::Theme *theme() const noexcept;
    void setTheme(const Markoff::Theme *theme);

    /// True for the synchronous duration of `applyOps` while D2 CRDT change
    /// notification is mutating model rows. LiveEditBinding queries this
    /// inside its `contentsChange` slot to suppress the synchronous
    /// QML-binding echo.
    bool applyingModelUpdate() const;

Q_SIGNALS:
    void documentChanged();
    void themeChanged();

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

Q_DECLARE_OPERATORS_FOR_FLAGS(Markoff::Live::LiveListModelBinding::Capabilities)
