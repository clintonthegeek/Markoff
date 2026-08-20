// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <unordered_map>

#include <QAccessibleWidget>

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
/// directly (not `QAccessibleWidget` — a block is not a `QWidget`).
///
/// **A1.1 status:** skeleton. `rect()` is real (maps `View::blockRect()`
/// through the viewport to global coordinates); `role()` is a placeholder
/// constant, not yet the per-`BlockKind` table — **A1.2 replaces role()**
/// with the real mapping (spec §4.2) and adds real `state()`. `text()` is
/// the `QAccessible::Text` (name/description/…) surface, not block
/// *content* — that is `QAccessibleTextInterface`, added in A2 via
/// `interface_cast`.
class CanvasBlockAccessible final : public QAccessibleInterface {
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

    BlockId blockId() const { return m_id; }

private:
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
