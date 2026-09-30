// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <QAccessible>
#include <QAccessibleWidget>
#include <QList>
#include <QVariant>

#include <markoff/core/BlockId.h>

namespace Markoff::Canvas {
class View;
}

namespace Markoff::Canvas::Detail {

class CanvasBlockAccessible;

/// std::unordered_map hasher for BlockId — the type only provides Qt's
/// qHash (used by QHash), and QHash's copy-on-write Node type can't hold a
/// move-only std::unique_ptr value (see m_children's own doc comment).
struct BlockIdHash {
    size_t operator()(BlockId id) const noexcept { return size_t(id.raw()); }
};

/// `View`'s document-container accessible (spec §4.1, A1 — the per-block
/// tree's root). Wraps the widget itself: role `Document`, children are one
/// `CanvasBlockAccessible` per `BlockId` in document order — the a11y tree
/// walks `View::blockCount()`, NOT `realizedBlockCount()` (spec §4.1/§5: the
/// tree is the document, not the viewport; only geometry queries force
/// realization).
///
/// Owns a lazily-populated cache of block accessibles keyed by `BlockId` —
/// separate from Qt's own `QAccessibleCache` (which only knows how to key
/// on `QObject*`, and block accessibles wrap no QObject). Eviction on block
/// removal is A3.3's job (CRDT-churn lifetime, spec §9 Q1); nothing evicts
/// yet, so a removed block's accessible is simply never returned again by
/// `child()`/`childAt()`, but its entry stays in `m_children` until that
/// task lands.
class CanvasAccessible final : public QAccessibleWidget {
public:
    explicit CanvasAccessible(View *view);
    ~CanvasAccessible() override;

    QAccessible::Role role() const override;
    QAccessible::State state() const override;
    /// `QAccessible::Name` resolution (spec §9 Q2, A1.3):
    /// `View::accessibleDocumentName()` -> `View::inlineTitle()` -> a
    /// generic `tr()`'d fallback. All other `QAccessible::Text` values
    /// fall through to `QAccessibleWidget`'s default.
    QString text(QAccessible::Text t) const override;

    int childCount() const override;
    QAccessibleInterface *child(int index) const override;
    QAccessibleInterface *childAt(int x, int y) const override;
    int indexOfChild(const QAccessibleInterface *child) const override;

    /// Non-owning: the (cached, created-on-demand) accessible for `id`, or
    /// nullptr if `id` is not in the current document. Shared by
    /// `child()`/`childAt()` and by `CanvasBlockAccessible::parent()`'s
    /// round trip back through this container.
    CanvasBlockAccessible *blockAccessible(BlockId id) const;

    /// A3.2 (spec §4.4): diff `View`'s caret/selection against what was last
    /// announced and emit `Focus` (caret entered a new block while the view
    /// has focus), `TextCaretMoved` (on the caret's block) and
    /// `TextSelectionChanged` (on each block whose per-block selection
    /// intersection changed, incl. shrinking to empty as `(-1,-1)`).
    /// Synchronous, idempotent (a repeated call with no change emits
    /// nothing), and a no-op unless `QAccessible::isActive()`.
    void syncTextNotifications(bool viewHasFocus);

    /// A3.2: `Focus` (gained) or `StateChanged{focused}` (lost) on the
    /// effective focus holder — the caret's block, else this container.
    void notifyFocusChange(bool gained);

    /// A3.3 (spec §4.4/§4.5/§9 Q1): reconcile this container against the
    /// view's CURRENT block list after a document change. (1) Per created
    /// block whose edit sequence moved: diff its last-seen text snapshot
    /// against the buffer (QChar common prefix/suffix, per block only, C4)
    /// and emit `TextRemoved` then `TextInserted`. (2) Blocks new to the id
    /// list get an accessible + `ObjectCreated`. (3) Created blocks no
    /// longer in the list get `ObjectDestroyed`, are dropped from
    /// `m_children` and released from Qt's cache. Snapshots/eviction always
    /// run (cheap, proportional to CREATED children — zero without an AT
    /// client); events are emitted only while `QAccessible::isActive()`.
    void syncStructure();

    /// A3.3: `View::setDocument()` swapped the document. BlockIds are only
    /// unique within one document (two docs can mint the same id), so id
    /// membership cannot tell old from new: release EVERY created block
    /// (`ObjectDestroyed` while active) and re-prime the id list silently
    /// (no `ObjectCreated` flood for a freshly loaded document).
    void resetForNewDocument();

    /// A4.1 (spec §4.3/§4.4): diff every CREATED block's fold-derived state
    /// (`expandable`, `expanded`, `invisible`) against what was last
    /// announced and emit one `QAccessibleStateChangeEvent` per changed
    /// flag. Called from `View::refreshFoldedBlocks()` — the single place
    /// every fold-state or hidden-projection change funnels through
    /// (`toggleFold`, `setFoldedHeadIndices`, document edits, the H-arc
    /// title hide) — so no path is missed. Synchronous (C2); a no-op unless
    /// `QAccessible::isActive()`. `childCount()` is never affected.
    void syncFoldNotifications();

private:
    void evict(BlockId id);

    /// A per-block selection intersection (bytes in the block's own buffer),
    /// `{-1,-1}` = none. Ordered document-space endpoints of the selection.
    struct SelSnapshot {
        bool valid = false;
        BlockId startBlock, endBlock;
        int startByte = 0, endByte = 0;
    };
    SelSnapshot currentSelection() const;

    View *m_view;
    /// Block accessibles have no QObject, so Qt's cache cannot key them
    /// off one; but an interface-built `QAccessibleEvent` registers its
    /// target in Qt's cache (`QAccessible::uniqueId`), and the cache then
    /// owns/`delete`s it. So blocks are registered with Qt at creation and
    /// Qt owns them (no unique_ptr — that would double-delete at shutdown);
    /// this container deletes them via `QAccessible::deleteAccessibleInterface`.
    struct Child {
        CanvasBlockAccessible *iface = nullptr;
        QAccessible::Id id = 0;
        /// A3.3: last-seen buffer state, for text insert/remove payloads.
        quint64 seq = 0;
        QString text;
        /// A4.1: last-announced fold-derived state bits (see foldBits()).
        quint8 foldBits = 0;
    };
    mutable std::unordered_map<BlockId, Child, BlockIdHash> m_children;

    // A3.3: block ids as of the last syncStructure() (document order).
    std::vector<BlockId> m_knownIds;
    bool m_primeSilently = false;

    // A3.2: last-announced state (see syncTextNotifications()).
    BlockId m_notifiedCaretBlock;
    int m_notifiedCaretByte = -1;
    SelSnapshot m_notifiedSel;
};

/// One per `BlockId` (spec §4.1/§4.2), implementing `QAccessibleInterface`
/// directly (not `QAccessibleWidget` — a block is not a `QWidget`) plus
/// `QAccessibleAttributesInterface` (heading level only) and, since A2.1,
/// `QAccessibleTextInterface` — both exposed via `interface_cast`.
///
/// **A1.1 status:** skeleton — superseded. `rect()` is real (maps
/// `View::blockRect()` through the viewport to global coordinates).
/// **A1.2:** `role()` implements the real spec §4.2 `BlockKind` → `Role`
/// table; `state()` sets `focusable`/`focused` (caret block),
/// `editable` (`View::isReadOnly()`), `checkable`/`checked` (task
/// `ListItem`s, from the `Checked` attr), and `invisible` (folded-hidden
/// blocks, `View::isBlockHidden()`). `attributeKeys()`/`attributeValue()`
/// implement `Attribute::Level` for `Heading` blocks — confirmed to reach
/// AT-SPI by A1.0's probe, so this is the sole mechanism (no description
/// fallback). `text()` is still the `QAccessible::Text` (name/
/// description/…) surface, not block *content* — that is
/// `QAccessibleTextInterface`.
///
/// **A2.1:** `QAccessibleTextInterface` core — `text()`, `characterCount()`,
/// and `textAtOffset`/`textBeforeOffset`/`textAfterOffset` for
/// `CharBoundary`/`WordBoundary`/`ParagraphBoundary` all operate on
/// `document()->blockText(id)`, converted byte<->QChar with `coords::`
/// (spec §4.1). **Every offset here is scoped to this block's own buffer —
/// never summed across blocks (C4).** `Char`/`WordBoundary` reuse the base
/// class's default `QTextBoundaryFinder`-driven implementation (it already
/// operates purely in QChar space via `text()`/`characterCount()`, so no
/// override is needed); `ParagraphBoundary` IS overridden because the base
/// default treats it as a line-break search, which would be wrong for a
/// `CodeBlock` buffer (its buffer keeps embedded `\n`s, spec §4.2's kind
/// table) — a block is always exactly one paragraph, full stop, per this
/// task's contract.
///
/// **A2.2:** `cursorPosition()`/`setCursorPosition()` and
/// `selection()`/`selectionCount()` are real — caret only reported by the
/// block `View::caretBlock()` currently holds; selection is this block's
/// intersection with the view's single anchor+caret selection, computed
/// entirely from `View`'s PUBLIC inspection surface (`hasSelection()`,
/// `selectionAnchorBlock()`/`ByteOffset()`, `caretBlock()`/`ByteOffset()`,
/// `blockIndexOf()`) — never the private `orderedSelection()`/
/// `selectedByteRangeInBlock()` pair `View.cpp` uses internally, since
/// growing the public surface or adding a friend isn't spec-authorized for
/// this task. `addSelection`/`removeSelection`/`setSelection` stay no-op
/// placeholders: `View` has no public selection-*setting* API (selection is
/// edited only through real input events), and A2.2's done-when doesn't
/// claim them — logged, not an oversight.
///
/// **A2.3:** `characterRect()`/`offsetAtPoint()` and
/// `textAtOffset(…, LineBoundary)` are real — all three need a realized
/// `QTextLayout`, so they call the new `View::ensureBlockRealized()` first
/// (spec §5: a geometry query is the one path allowed to force realization,
/// bounded to exactly the queried block). `characterRect()`/
/// `offsetAtPoint()` are in GLOBAL screen coordinates (same convention
/// `rect()` already uses via `blockGlobalRect()`), built on two new `View`
/// geometry accessors (`characterRectInViewport()`, and
/// `byteOffsetAtPoint()` for the point->offset direction — `hitTest()`
/// itself is private and does a whole-document Y-dispatch this class
/// doesn't need; `offsetAtPoint()` here gates on `rect().contains(point)`
/// first to reject a point belonging to some OTHER block). `textAtOffset(…,
/// LineBoundary)` uses a third new
/// accessor, `View::lineByteRangeAt()`, which reads the actual WRAPPED
/// visual line off the layout — deliberately not the base class's default
/// (literal `\n`-splitting the plain string, wrong for a wrapped paragraph
/// with no embedded `\n` at all). `textBeforeOffset`/`textAfterOffset` for
/// `LineBoundary` are NOT overridden — outside this task's named scope
/// (plan: "…and `textAtOffset(…, LineBoundary)`" only) — so they still fall
/// through to the base class's `\n`-based approximation; logged, not an
/// oversight. `scrollToSubstring` is a no-op stub (no task claims it yet);
/// `attributes()` returns an empty string with `startOffset`/`endOffset`
/// set to the queried offset (same placeholder shape).
class CanvasBlockAccessible final : public QAccessibleInterface,
                                     public QAccessibleAttributesInterface,
                                     public QAccessibleTextInterface,
                                     public QAccessibleEditableTextInterface,
                                     public QAccessibleActionInterface {
public:
    CanvasBlockAccessible(View *view, CanvasAccessible *container, BlockId id);

    bool isValid() const override;
    QObject *object() const override;

    QAccessibleInterface *childAt(int x, int y) const override;
    QAccessibleInterface *parent() const override;
    QAccessibleInterface *child(int index) const override;
    int childCount() const override;
    int indexOfChild(const QAccessibleInterface *child) const override;

    QString text(QAccessible::Text t) const override;
    void setText(QAccessible::Text t, const QString &text) override;
    QRect rect() const override;
    QAccessible::Role role() const override;
    QAccessible::State state() const override;
    void *interface_cast(QAccessible::InterfaceType t) override;

    // ---- QAccessibleAttributesInterface (heading level only, spec §4.6
    // finding 3 / A1.0) --------------------------------------------------
    QList<QAccessible::Attribute> attributeKeys() const override;
    QVariant attributeValue(QAccessible::Attribute key) const override;

    // ---- QAccessibleTextInterface (A2.1: text/characterCount/offsets) ----
    // text/cursor
    QString text(int startOffset, int endOffset) const override;
    QString textBeforeOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                              int *startOffset, int *endOffset) const override;
    QString textAfterOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                             int *startOffset, int *endOffset) const override;
    QString textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                          int *startOffset, int *endOffset) const override;
    int characterCount() const override;

    // selection (A2.2) — this block's intersection with the view's single
    // anchor+caret selection. add/remove/setSelection stay no-op: no public
    // View API to route a programmatic selection-set through (see class
    // doc comment).
    void selection(int selectionIndex, int *startOffset, int *endOffset) const override;
    int selectionCount() const override;
    void addSelection(int startOffset, int endOffset) override;
    void removeSelection(int selectionIndex) override;
    void setSelection(int selectionIndex, int startOffset, int endOffset) override;

    // cursor (A2.2) — only the block View::caretBlock() currently holds
    // reports a position; setCursorPosition() routes to
    // View::setCaretPosition().
    int cursorPosition() const override;
    void setCursorPosition(int position) override;

    // character <-> geometry (A2.3) — force realization of this block via
    // View::ensureBlockRealized() first (spec §5), global screen coords.
    QRect characterRect(int offset) const override;
    int offsetAtPoint(const QPoint &point) const override;

    // Not claimed by any task yet; safe no-op / minimal placeholders.
    void scrollToSubstring(int startIndex, int endIndex) override;
    QString attributes(int offset, int *startOffset, int *endOffset) const override;

    // ---- QAccessibleEditableTextInterface (A4.2) -------------------------
    // Present exactly where the text interface is. Every method is one
    // block-scoped replace (offsets are this block's QChar offsets) routed
    // through View's IME-commit path — see replaceRange() in the .cpp.
    // Read-only mode and out-of-range/invalid input reject with no effect.
    void deleteText(int startOffset, int endOffset) override;
    void insertText(int offset, const QString &text) override;
    void replaceText(int startOffset, int endOffset, const QString &text) override;

    // ---- QAccessibleActionInterface (A4.1) -------------------------------
    // Only a fold head (`View::isBlockFoldable`) has actions: a single
    // `toggleAction()` wired to `View::toggleFold()`. Any other block has
    // no action interface (interface_cast returns nullptr).
    QStringList actionNames() const override;
    QString localizedActionName(const QString &name) const override;
    QString localizedActionDescription(const QString &name) const override;
    void doAction(const QString &actionName) override;
    QStringList keyBindingsForAction(const QString &actionName) const override;

    BlockId blockId() const { return m_id; }

private:
    void replaceRange(int startOffset, int endOffset, const QString &text);

    /// Blocks with no text content (`HorizontalRule`, `Image`, `Mermaid` —
    /// spec §4.2) return `nullptr` for `QAccessible::TextInterface` from
    /// `interface_cast` rather than implementing it vacuously.
    bool hasTextContent() const;

    View *m_view;
    CanvasAccessible *m_container;
    BlockId m_id;
};

/// Installs the `QAccessible::InstallFactory` that resolves a `View`
/// instance to its `CanvasAccessible`. Idempotent — safe to call from every
/// `View` constructor (spec §4.5: repeated `View` construction must not
/// re-register).
void installAccessibilityFactory();

/// A3.2: `View`'s emit hooks (spec §4.4). Both return immediately unless
/// `QAccessible::isActive()`, and both emit synchronously (C2).
void notifyTextState(View *view, bool viewHasFocus);
void notifyFocusChange(View *view, bool gained);

/// A4.1: called at the end of `View::refreshFoldedBlocks()` — fold /
/// hidden-projection state may have changed (see
/// `CanvasAccessible::syncFoldNotifications`). Returns at once unless
/// `QAccessible::isActive()`.
void notifyFoldState(View *view);

/// A3.3: called from `View::onDocumentChanged()` — text insert/remove events,
/// block ObjectCreated/ObjectDestroyed, and eviction of removed blocks'
/// accessibles. Returns at once if `view` has no container (no AT client has
/// ever touched it), so a plain non-a11y app pays one hash lookup.
void notifyDocumentChanged(View *view);

/// A3.3: `View::setDocument()` swapped documents (see
/// `CanvasAccessible::resetForNewDocument`).
void notifyDocumentReplaced(View *view);

}  // namespace Markoff::Canvas::Detail
