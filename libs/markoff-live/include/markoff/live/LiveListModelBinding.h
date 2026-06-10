// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockHitTester.h>
#include <markoff/live/LiveStructuralKeyHandler.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/LiveClipboardController.h>
#include <markoff/live/LiveActionController.h>
#include <markoff/live/LiveFormatController.h>
#include <markoff/live/LiveContextMenuHandler.h>

#include <QObject>
#include <QAbstractListModel>
#include <memory>
#include <qqmlintegration.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff/core/Session.h>
#include <markoff/core/Theme.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/DefaultLinkService.h>
#include <markoff/core/FindController.h>

namespace Markoff::Live {

inline constexpr qreal kMinFontScale     = 0.5;
inline constexpr qreal kMaxFontScale     = 3.0;
inline constexpr qreal kFontScaleStep    = 1.10;
inline constexpr qreal kDefaultFontScale = 1.0;

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
    Q_PROPERTY(Markoff::Live::LiveStructuralKeyHandler *structuralKeyHandler
               READ structuralKeyHandler CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveNavigationController *navigationController
               READ navigationController CONSTANT)
    Q_PROPERTY(QAbstractListModel *remoteCursorsModel
               READ remoteCursorsModel CONSTANT)
    Q_PROPERTY(const Markoff::Theme *theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(qreal fontScale     READ fontScale     WRITE setFontScale NOTIFY fontScaleChanged)
    Q_PROPERTY(qreal fontScaleStep READ fontScaleStep CONSTANT)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    // Attach-window contract (2026-06-10): true once the consumer has
    // explicitly placed the caret on the current document. LiveView.qml's
    // initial-focus seed (onCountChanged → requestTextCaretAtRow(0,0))
    // checks this and yields, so a setCursorPosition() issued in the same
    // call stack as setDocument() is not clobbered by the seed firing one
    // frame later. Reset on every setDocument().
    Q_PROPERTY(bool initialCaretRequested READ initialCaretRequested
               NOTIFY initialCaretRequestedChanged)

    Q_PROPERTY(Markoff::Live::LiveClipboardController *clipboardController
               READ clipboardController CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveActionController *actionController
               READ actionController CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveFormatController *formatController
               READ formatController CONSTANT)
    Q_PROPERTY(Markoff::Live::LiveContextMenuHandler *contextMenuHandler
               READ contextMenuHandler CONSTANT)

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

    /// Flushes any queued `d2DocumentChanged` signal synchronously so the
    /// model reflects all pending CRDT edits before the caller proceeds.
    /// No-op when nothing is pending. Use from focus-resolution paths
    /// (cursor request + delegate registration) that need the model to be
    /// current. Queue #2 concern #5 — gives `LiveCursorState` a binding-
    /// level entry instead of reaching into `document()->flushPendingD2Changed()`
    /// directly.
    Q_INVOKABLE void flushPendingDocumentChanges();

    /// Bind a Session so that Session::primarySelectionChanged propagates into
    /// the SelectionView. Pass nullptr to detach. The binding does NOT take
    /// ownership of the session (it is owned by the MarkoffDocument).
    Q_INVOKABLE void setSession(Markoff::Session *session);

    /// Attach-window contract (see the Q_PROPERTY note above). C++-side
    /// marker called by EditorWidget::setCursorPosition; not Q_INVOKABLE —
    /// QML never sets it, only reads it in the initial-focus seed guard.
    bool initialCaretRequested() const noexcept;
    void markInitialCaretRequested();

    LiveBlockModel           *model()               const;
    LiveCursorState          *cursorState()         const;
    BlockHitTester           *hitTester()           const;
    LiveStructuralKeyHandler *structuralKeyHandler() const;
    LiveNavigationController *navigationController() const;
    const BlockKindRegistry  *registry()            const;
    QAbstractListModel       *remoteCursorsModel()  const;

    LiveClipboardController *clipboardController() const;
    LiveActionController    *actionController()    const;
    LiveFormatController    *formatController()    const;
    LiveContextMenuHandler  *contextMenuHandler()  const;

    const Markoff::Theme *theme() const noexcept;
    void setTheme(const Markoff::Theme *theme);
    Q_INVOKABLE void applyDefaultTheme(bool dark);

    // Theme accessors callable from QML. `Markoff::Theme` is a Q_GADGET; QML
    // cannot dispatch methods through a `QVariant(const Theme*)` returned from
    // the `theme` property, so delegates route theme lookups through these
    // Q_INVOKABLE proxies on the Q_OBJECT binding instead. `slot` matches the
    // integer value of `Markoff::Theme::Slot` (0 = TextDefault, 1..6 =
    // Heading1..Heading6, etc.).
    Q_INVOKABLE qreal   themePixelSizeFor(int slot) const;
    Q_INVOKABLE QString themeFamilyFor(int slot) const;
    Q_INVOKABLE bool    themeIsBold(int slot) const;
    Q_INVOKABLE bool    themeIsItalic(int slot) const;
    Q_INVOKABLE QColor  themeColorFor(int slot) const;

    qreal fontScale()     const noexcept;
    qreal fontScaleStep() const noexcept { return kFontScaleStep; }
    void  setFontScale(qreal s);

    /// Single authority for the read-only state (contract-v2 spec §4.2).
    /// Every mutation-ingress gate (LiveEditBinding, LiveStructuralKeyHandler,
    /// LiveClipboardController, TableEditBinding, LiveActionController) reads
    /// this flag — no second store. Emits readOnlyChanged only on change.
    bool readOnly() const noexcept;
    void setReadOnly(bool ro);

    /// True for the synchronous duration of `applyOps` while D2 CRDT change
    /// notification is mutating model rows. LiveEditBinding queries this
    /// inside its `contentsChange` slot to suppress the synchronous
    /// QML-binding echo.
    bool applyingModelUpdate() const;

    Markoff::LinkService *linkService() const;
    Q_INVOKABLE void setLinkService(Markoff::LinkService *service);

    QString fromContext() const;
    Q_INVOKABLE void setFromContext(const QString &);

    /// Activate the link span (if any) that covers `qtPos` in the block
    /// identified by `blockId`. `modifiers` is the raw Qt::KeyboardModifiers
    /// int (passes cleanly through QML Q_INVOKABLE calls). No-op if no link
    /// span covers qtPos. Images are not activated in E3a.
    Q_INVOKABLE void activateLinkAt(Markoff::BlockId blockId, int qtPos, int modifiers);

    /// Notify the link service that the cursor is hovering over the link span
    /// (if any) that covers `qtPos` in `blockId`. Emits notifyHover on entry
    /// and notifyHoverLeft on span transition. Returns true if a link was hit.
    /// Idempotent within the same span — repeated calls while the cursor stays
    /// on the same rawText do not re-emit notifyHover.
    Q_INVOKABLE bool hoverLinkAt(Markoff::BlockId blockId, int qtPos, int modifiers,
                                 const QPoint &globalPos);

    /// Notify the link service that hover has left the current span (if any).
    /// No-op when no span is currently hovered. Idempotent.
    Q_INVOKABLE void clearLinkHover();

    /// Attaches a Markoff::FindController so the live leaf renders its
    /// matches and responds to navigation. The controller is owned by
    /// the consumer; the binding does not take ownership. Pass nullptr
    /// or call detachFindController() to disconnect.
    Q_INVOKABLE void attachFindController(Markoff::FindController *fc);
    Q_INVOKABLE void detachFindController();

Q_SIGNALS:
    void documentChanged();
    void themeChanged();
    void fontScaleChanged();
    void readOnlyChanged();
    void initialCaretRequestedChanged();
    void linkServiceChanged();
    void fromContextChanged();


private:
    void onD2Changed();

    struct Private;
    std::unique_ptr<Private> d;

    std::unique_ptr<Markoff::DefaultLinkService> m_defaultLinkService;
    Markoff::LinkService *m_linkService = nullptr;
    QString m_fromContext;
    QString m_currentHoveredRawText;
};

}  // namespace Markoff::Live

Q_DECLARE_OPERATORS_FOR_FLAGS(Markoff::Live::LiveListModelBinding::Capabilities)
