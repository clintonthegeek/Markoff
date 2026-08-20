// SPDX-License-Identifier: GPL-3.0-or-later
#include "Accessibility.h"

#include <QScrollBar>
#include <QWidget>

#include <markoff/canvas/View.h>

namespace Markoff::Canvas::Detail {

namespace {

/// `View::blockRect(id)`'s document-coordinate rect, mapped through the
/// current scroll offset and the viewport widget to global (screen)
/// coordinates — the coordinate space every `QAccessibleInterface::rect()`
/// and `childAt(x, y)` contract is defined in. Null `QRect` if `id` isn't
/// currently realized (`View::blockRect`'s own contract) — geometry needs
/// layout (spec §5); an AT client asking for an unrealized block's rect
/// gets nothing until something (this leaf's own scroll/realize path)
/// realizes it, same as every other geometry accessor in this leaf.
QRect blockGlobalRect(View *view, BlockId id)
{
    const QRectF doc = view->blockRect(id);
    if (doc.isNull())
        return {};
    const qreal scrollY = view->verticalScrollBar()->value();
    const QPoint topLeftViewport(qRound(doc.x()), qRound(doc.y() - scrollY));
    const QPoint topLeftGlobal = view->viewport()->mapToGlobal(topLeftViewport);
    return QRect(topLeftGlobal, QSize(qRound(doc.width()), qRound(doc.height())));
}

}  // namespace

// ---- CanvasAccessible ------------------------------------------------

CanvasAccessible::CanvasAccessible(View *view)
    : QAccessibleWidget(view, QAccessible::Document)
    , m_view(view)
{
}

CanvasAccessible::~CanvasAccessible() = default;

QAccessible::Role CanvasAccessible::role() const
{
    return QAccessible::Document;
}

QAccessible::State CanvasAccessible::state() const
{
    QAccessible::State s = QAccessibleWidget::state();
    s.editable = !m_view->isReadOnly();
    return s;
}

int CanvasAccessible::childCount() const
{
    return m_view->blockCount();
}

QAccessibleInterface *CanvasAccessible::child(int index) const
{
    const BlockId id = m_view->blockIdAt(index);
    if (id.isNull())
        return nullptr;
    return blockAccessible(id);
}

QAccessibleInterface *CanvasAccessible::childAt(int x, int y) const
{
    const int n = m_view->blockCount();
    for (int i = 0; i < n; ++i) {
        const BlockId id = m_view->blockIdAt(i);
        if (id.isNull())
            continue;
        if (blockGlobalRect(m_view, id).contains(QPoint(x, y)))
            return blockAccessible(id);
    }
    return nullptr;
}

int CanvasAccessible::indexOfChild(const QAccessibleInterface *child) const
{
    const auto *block = dynamic_cast<const CanvasBlockAccessible *>(child);
    if (!block)
        return -1;
    return m_view->blockIndexOf(block->blockId());
}

CanvasBlockAccessible *CanvasAccessible::blockAccessible(BlockId id) const
{
    if (id.isNull())
        return nullptr;
    auto it = m_children.find(id);
    if (it != m_children.end())
        return it->second.get();
    auto block = std::make_unique<CanvasBlockAccessible>(m_view, const_cast<CanvasAccessible *>(this), id);
    auto *raw = block.get();
    m_children.emplace(id, std::move(block));
    return raw;
}

// ---- CanvasBlockAccessible ---------------------------------------------

CanvasBlockAccessible::CanvasBlockAccessible(View *view, CanvasAccessible *container, BlockId id)
    : m_view(view)
    , m_container(container)
    , m_id(id)
{
}

bool CanvasBlockAccessible::isValid() const
{
    return m_view && m_view->blockIndexOf(m_id) >= 0;
}

QObject *CanvasBlockAccessible::object() const
{
    return nullptr;
}

QAccessibleInterface *CanvasBlockAccessible::childAt(int, int) const
{
    return nullptr;
}

QAccessibleInterface *CanvasBlockAccessible::parent() const
{
    return m_container;
}

QAccessibleInterface *CanvasBlockAccessible::child(int) const
{
    return nullptr;
}

int CanvasBlockAccessible::childCount() const
{
    return 0;
}

int CanvasBlockAccessible::indexOfChild(const QAccessibleInterface *) const
{
    return -1;
}

QString CanvasBlockAccessible::text(QAccessible::Text) const
{
    // A1.1 skeleton — no name/description surface yet. A1.2 fills in
    // role-appropriate description text (spec §4.2's per-kind notes, e.g.
    // CodeBlock language, Math source); the container's Name resolution
    // (accessibleDocumentName -> inlineTitle -> generic) is A1.3's, and is
    // a CanvasAccessible concern, not this class's.
    return {};
}

void CanvasBlockAccessible::setText(QAccessible::Text, const QString &)
{
    // No settable accessible text at this stage.
}

QRect CanvasBlockAccessible::rect() const
{
    return blockGlobalRect(m_view, m_id);
}

QAccessible::Role CanvasBlockAccessible::role() const
{
    // A1.1 placeholder — every block reports the same role regardless of
    // BlockKind. A1.2 replaces this with the real §4.2 mapping table.
    return QAccessible::StaticText;
}

QAccessible::State CanvasBlockAccessible::state() const
{
    // A1.1 placeholder — no state bits set. A1.2 adds focusable/focused/
    // editable/checkable/invisible per §4.2/§4.3.
    return {};
}

// ---- Factory registration ----------------------------------------------

namespace {

QAccessibleInterface *canvasAccessibleFactory(const QString &classname, QObject *object)
{
    if (classname == QLatin1String("Markoff::Canvas::View")) {
        if (auto *view = qobject_cast<View *>(object))
            return new CanvasAccessible(view);
    }
    return nullptr;
}

}  // namespace

void installAccessibilityFactory()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    QAccessible::installFactory(canvasAccessibleFactory);
}

}  // namespace Markoff::Canvas::Detail
