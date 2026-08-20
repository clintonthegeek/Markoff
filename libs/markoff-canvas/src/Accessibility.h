// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <unordered_map>

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

private:
    View *m_view;
    mutable std::unordered_map<BlockId, std::unique_ptr<CanvasBlockAccessible>, BlockIdHash> m_children;
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
/// task's contract. The remaining pure virtuals
/// (`selection`/`selectionCount`/`addSelection`/`removeSelection`/
/// `setSelection`/`cursorPosition`/`setCursorPosition` — A2.2's job;
/// `characterRect`/`offsetAtPoint` — A2.3's job) are compile-complete
/// placeholder stubs, documented at each definition, following A1.1's
/// "compile-complete but explicitly-placeholder" precedent for `role()`/
/// `state()`. `scrollToSubstring` is a no-op stub (no task claims it yet);
/// `attributes()` returns an empty string with `startOffset`/`endOffset`
/// set to the queried offset (same placeholder shape).
class CanvasBlockAccessible final : public QAccessibleInterface,
                                     public QAccessibleAttributesInterface,
                                     public QAccessibleTextInterface {
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

    // selection — A2.2 placeholder stubs (this block never reports a
    // selection yet).
    void selection(int selectionIndex, int *startOffset, int *endOffset) const override;
    int selectionCount() const override;
    void addSelection(int startOffset, int endOffset) override;
    void removeSelection(int selectionIndex) override;
    void setSelection(int selectionIndex, int startOffset, int endOffset) override;

    // cursor — A2.2 placeholder stubs (this block never reports a caret
    // yet).
    int cursorPosition() const override;
    void setCursorPosition(int position) override;

    // character <-> geometry — A2.3 placeholder stubs (need a QTextLayout,
    // spec §5).
    QRect characterRect(int offset) const override;
    int offsetAtPoint(const QPoint &point) const override;

    // Not claimed by any task yet; safe no-op / minimal placeholders.
    void scrollToSubstring(int startIndex, int endIndex) override;
    QString attributes(int offset, int *startOffset, int *endOffset) const override;

    BlockId blockId() const { return m_id; }

private:
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

}  // namespace Markoff::Canvas::Detail
