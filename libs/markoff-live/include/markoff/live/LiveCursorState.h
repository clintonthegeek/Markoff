// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/live/Cursor.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <optional>
#include <qqmlintegration.h>

// Session bridge (tier 4c Phase A): full Selection definition needed for slot signature.
#include <markoff/core/Selection.h>

namespace Markoff {
class Session;
}  // namespace Markoff

namespace Markoff::Live {

class BlockKindRegistry;
class LiveBlockModel;
class LiveListModelBinding;

/// Selection anchor — the "other end" of a text selection. The active
/// end is `m_cursor` (variant: TextCaret). When a selection is active,
/// `m_selectionAnchor` holds the BlockAnchor + qtPos where the selection
/// started (anchored by Click, Shift+Click `begin`, or session-incoming).
/// Identity is by `BlockAnchor` (stable across structural edits); the
/// inner-row index is derived on demand. Tier 4c canonical store.
struct SelectionAnchor {
    Markoff::BlockAnchor block;
    quint32              qtPos;
    bool operator==(const SelectionAnchor &other) const noexcept {
        return block == other.block && qtPos == other.qtPos;
    }
};

/// Owns the canonical cursor value for **structural events** (kind
/// transitions, cross-block navigation, `BlockSelected`, `BlockInternalEdit`).
/// For **in-block caret position during typing**, `QQuickTextEdit::cursorPosition`
/// is canonical; `m_cursor` mirrors it via `syncFromTextEdit`, called from each
/// text-bearing delegate's `onCursorPositionChanged` and from
/// `LiveEditBinding::onContentsChange` after each buffer edit. The authority
/// split is documented in `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md`
/// §3.
///
/// `cursorKind` Q_PROPERTY exposes the active variant as a string for QML
/// bindings that need to react to focus type (e.g. focus ring vs. caret).
/// Values: "none", "TextCaret", "BlockSelected", "BlockInternalEdit".
///
/// `requestTextCaretAtRow` is a row-keyed convenience wrapper over
/// `establishFocus`: it flushes any pending document changes (via
/// `LiveListModelBinding::flushPendingDocumentChanges`), resolves the
/// row to a `BlockAnchor` via `recordAt(row).blockAnchor`, and stages
/// the chokepoint pending. Out-of-range rows are rejected synchronously.
/// `establishFocus` is the canonical entry for callers that already
/// hold a `BlockAnchor` (e.g. `LiveStructuralKeyHandler` consuming
/// `Cmd::*` return values). Spec
/// `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md` §3.
class MARKOFF_LIVE_EXPORT LiveCursorState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveCursorState is provided by LiveListModelBinding")

    Q_PROPERTY(QString cursorKind READ cursorKind NOTIFY cursorChanged)
    /// The inner-model row index for the active anchor-side TextCaret, else -1.
    /// QML uses this directly as the ListView row index to find the delegate
    /// that should receive keyboard focus after a structural edit (mid-block
    /// split, Backspace-merge, Delete-merge, marker insertion).
    Q_PROPERTY(int focusedAnchorRow READ focusedAnchorRow NOTIFY cursorChanged)
    /// The qtPos of the active TextCaret, else -1.
    Q_PROPERTY(int focusedQtPos READ focusedQtPos NOTIFY cursorChanged)
    /// Cross-block column-preservation state. Set by LiveNavigationController
    /// before each Up/Down cross; cleared on Left/Right or any non-vertical
    /// motion. -1.0 sentinel = unset. Spec §4.5 lifecycle rules.
    Q_PROPERTY(qreal desiredVisualX READ desiredVisualX
                                    WRITE setDesiredVisualX
                                    NOTIFY desiredVisualXChanged)
    /// Cross-block column-preservation visual-line hint. Set by
    /// `LiveNavigationController` before each Up/Down cross to indicate
    /// which visual line of the destination block the caret should land on
    /// (FirstLine for Up, LastLine for Down). Cleared back to None inside
    /// `tryResolvePending` after `takeFocus` returns (the destination
    /// delegate has consumed the hint by that point). Read by QML
    /// delegates' focusEditAt + onCursorChanged paths.
    Q_PROPERTY(VisualLineHint pendingVisualLineHint READ pendingVisualLineHint
                                                    NOTIFY visualLineHintChanged)

public:
    explicit LiveCursorState(const BlockKindRegistry *registry,
                             const LiveBlockModel    *model,
                             LiveListModelBinding    *binding,
                             QObject                 *parent = nullptr);

    Cursor cursor() const { return m_cursor; }
    /// Typed accessor for the TextCaret variant — returns `nullopt` when
    /// the cursor is in any other state (`NoCursor`, `BlockSelected`,
    /// `BlockInternalEdit`). Use this in place of `cursor()` followed by
    /// `std::get_if<TextCaret>(&...)` on a local copy. Queue #2 concern #12.
    std::optional<TextCaret> currentTextCaret() const {
        if (const auto *tc = std::get_if<TextCaret>(&m_cursor))
            return *tc;
        return std::nullopt;
    }
    QString cursorKind() const;
    int focusedAnchorRow() const;
    int focusedQtPos() const;

    qreal desiredVisualX() const noexcept { return m_desiredVisualX; }
    void  setDesiredVisualX(qreal x);
    Q_INVOKABLE void clearDesiredVisualX();

    enum class VisualLineHint { None, FirstLine, LastLine };
    Q_ENUM(VisualLineHint)

    /// Like requestTextCaretAtRow, but qtPos is computed by the destination
    /// delegate from desiredVisualX projected onto the line indicated by hint.
    void requestTextCaretAtRowVisualX(int expectedRow, VisualLineHint hint);
    VisualLineHint pendingVisualLineHint() const noexcept { return m_pendingVlhint; }

    void request(const Cursor &newCursor);
    void clear();

    int rowForBlock(const Markoff::BlockAnchor &block) const;

    /// Request a TextCaret at `qtPos` of the row at `expectedRow` once it
    /// exists. If the row already exists, equivalent to constructing a
    /// TextCaret from `model->recordAt(expectedRow).blockAnchor` and
    /// calling `request()`. If the row does not yet exist (because a
    /// structural edit was applied and the parse-back hasn't created it),
    /// record the request and watch `model->rowsInserted` for resolution.
    /// Pending requests linger up to two parse cycles before being
    /// dropped (see spec §8.4). Spec §5.3 step 6.
    ///
    /// If a request is already pending, it is replaced (latest-request-wins).
    Q_INVOKABLE void requestTextCaretAtRow(int expectedRow, int qtPos);

    /// One-way sync from QML `TextEdit::cursorPosition` → canonical
    /// `m_cursor`. Called by each text-bearing delegate's
    /// `onCursorPositionChanged` and by `LiveEditBinding::onContentsChange`
    /// after each buffer edit. Without this hook, `m_cursor` desyncs as
    /// the user types or moves within a block (because TextEdit handles
    /// within-block arrows and IME natively), and any subsequent kind
    /// transition or structural diff reads a stale qtPos and lands the
    /// caret in the wrong place. Idempotent if the cursor is already at
    /// `(anchor, qtPos)`. Deliberately does NOT reset `m_pendingFocus` — a
    /// pending chokepoint request must survive incidental TextEdit cursor
    /// moves until its delegate-registration event arrives.
    Q_INVOKABLE void syncFromTextEdit(Markoff::BlockAnchor anchor, int qtPos);

    // --- §5.1 focus-chokepoint additions (tier 1) ---

    /// Spec §5.1. Structural-event sites call this. Always stores as
    /// pending; resolution attempted at safe points (after delegate
    /// registration, at cascade end). Never dispatches synchronously
    /// from this call — see spec §5.1.2.
    Q_INVOKABLE void establishFocus(Markoff::BlockAnchor blockAnchor, int qtPos);

    /// Spec §5.1. Called by LiveListModelBinding at the top of
    /// onD2Changed, before any structural mutation. Suppresses
    /// resolution attempts during the cascade.
    void beginStructuralCascade();

    /// Spec §5.1. Called by LiveListModelBinding at the bottom of
    /// onD2Changed, after applyOps. Triggers a pending-resolution
    /// attempt with the now-current m_delegates.
    void endStructuralCascade();

    /// Spec §5.1. Each text-bearing delegate calls this from
    /// Component.onCompleted. `kind` is validated against the model
    /// on every resolution attempt (spec §5.1.1).
    Q_INVOKABLE void delegateAvailable(Markoff::BlockAnchor blockAnchor,
                                       const QString &kind,
                                       QQuickItem *delegateRoot);

    /// Spec §5.1. Called from Component.onDestruction. `delegateRoot` is the
    /// dying delegate's root; when a DelegateChooser kind-swap fires the new
    /// delegate's onCompleted *before* the old's onDestruction, the entry
    /// stored against `blockAnchor` already belongs to the replacement —
    /// passing the root lets the chokepoint skip the stale remove. May be
    /// `nullptr` from older callers (unconditional remove, legacy behaviour).
    Q_INVOKABLE void delegateGoingAway(Markoff::BlockAnchor blockAnchor,
                                       QQuickItem *delegateRoot = nullptr);

    // --- test-only helpers ---
    /// Attaches (or replaces) the LiveBlockModel used for kindFor lookups.
    /// Production path wires this in LiveListModelBinding; tests call directly.
    void attachModel(const LiveBlockModel *model);

    /// Returns true if a PendingFocus is currently stored. Test-only.
    bool hasPendingFocus() const noexcept { return m_pendingFocus.has_value(); }

    /// Returns true if the anchor has a record in m_delegates. Test-only.
    bool isDelegateRegistered(Markoff::BlockAnchor anchor) const {
        return m_delegates.contains(anchor);
    }

    // ---- Selection state (tier 4c) ----

    /// True when a selection is active — i.e., `m_selectionAnchor` is set
    /// AND it points to a different (block, qtPos) than the cursor's
    /// active end. Collapsed selections (anchor == active) report false.
    bool hasSelection() const noexcept;

    /// The anchor end of an active text selection, or nullopt if no
    /// selection is active. The active end is read from `cursor()` /
    /// `currentTextCaret()`.
    std::optional<SelectionAnchor> selectionAnchor() const noexcept {
        return m_selectionAnchor;
    }

    /// Sets the selection anchor. Used by `LiveSelectionView::begin` (and
    /// equivalent paths) to park the anchor at the click-time position.
    /// Does NOT mutate `m_cursor` — caller is responsible for moving the
    /// active end via `establishFocus` or `syncFromTextEdit`. Emits
    /// `selectionChanged` if the value changes.
    void setSelectionAnchor(SelectionAnchor anchor);

    /// Clears the selection anchor. Used by `LiveSelectionView::clear`
    /// and the orphaned-anchor branch in session-incoming. Does NOT
    /// mutate `m_cursor`. Emits `selectionChanged` if the slot was set.
    void clearSelectionAnchor() noexcept;

    /// Selection range for `row`, or `QPoint(-1, -1)` if untouched. The
    /// `end` component may be `INT_MAX` ("to end of block") — consumers
    /// must clamp via `Math.min(r.y, textEdit.length)` before calling
    /// `TextEdit.select`. Mirror of `LiveSelectionView::rangeForBlock`
    /// during Phase A; will replace it in Phase C.
    QPoint selectionRangeForBlock(int row) const;

    /// Copy the current selection to the system clipboard. Reads block
    /// texts from `m_model`. Mirror of `LiveSelectionView::copyToClipboard`
    /// during Phase A.
    void copySelectionToClipboard() const;

    /// Select all text in the document. Mutates both cursor active end
    /// (places at end of last block) and selection anchor (places at
    /// start of first block). Mirror of `LiveSelectionView::selectAll`
    /// during Phase A.
    void selectAllBlocks();

    /// Delete the currently-selected range and clear the selection.
    /// Mirror of `LiveSelectionView::deleteSelection` during Phase A.
    void deleteSelectionRange();

    /// Wires up the Session for primary-selection round-trips. Idempotent.
    /// On change, disconnects from the prior session before reconnecting.
    /// During Phase A this is added alongside the existing path in
    /// `LiveSelectionView::setSession`; the binding wires both. In Phase D
    /// the LiveSelectionView path is removed.
    void setSession(Markoff::Session *session);

    /// Outgoing: pushes the canonical selection state to
    /// `m_session->setPrimarySelection`. No-op if no session or no model.
    /// Mirror of `LiveSelectionView::syncToSession` during Phase A.
    void syncSelectionToSession();

Q_SIGNALS:
    void cursorChanged();
    void desiredVisualXChanged();
    void visualLineHintChanged();
    void selectionChanged();

private:
    bool validateVariant(const Cursor &c) const;

    Cursor                   m_cursor;
    const BlockKindRegistry *m_registry;
    const LiveBlockModel    *m_model;
    LiveListModelBinding    *m_binding = nullptr;
    qreal                    m_desiredVisualX = -1.0;
    VisualLineHint           m_pendingVlhint  = VisualLineHint::None;

    // --- §5.1 focus-chokepoint additions ---
    struct DelegateRecord {
        QString kind;
        QPointer<QQuickItem> root;
    };
    struct PendingFocus {
        Markoff::BlockAnchor target;
        int qtPos;
        qint64 enqueuedMs;
    };

    void tryResolvePending();
    void expireIfTimedOut(PendingFocus &p);  // body in Task 7

    std::optional<PendingFocus>                 m_pendingFocus;
    QHash<Markoff::BlockAnchor, DelegateRecord> m_delegates;
    bool                                        m_inStructuralCascade = false;

    static constexpr qint64 kPendingFocusTimeoutMs = 500;

    // Tier 4c — selection anchor (canonical store; the active end is m_cursor).
    std::optional<SelectionAnchor> m_selectionAnchor;

    // Session bridge (tier 4c Phase A).
    Markoff::Session *m_session = nullptr;

private Q_SLOTS:
    void onSessionPrimarySelectionChanged(const Markoff::Selection &sel);
};

}  // namespace Markoff::Live
