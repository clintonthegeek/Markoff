// SPDX-License-Identifier: GPL-3.0-or-later
#include "Accessibility.h"

#include <variant>

#include <QScrollBar>
#include <QWidget>

#include <markoff/canvas/View.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>

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

/// Reads a `bool`-typed attr off `id`, defaulting to `false` if the attr is
/// absent or holds a different alternative — same `constFind` +
/// `std::get_if` idiom every other reader in this leaf uses
/// (`BlockPresentation.cpp`, `View.cpp`, …), never a wrapper of its own.
bool boolAttr(MarkoffDocument *doc, BlockId id, const QByteArray &name)
{
    const auto attrs = doc->blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.cend())
        return false;
    const bool *v = std::get_if<bool>(&it.value());
    return v && *v;
}

/// Reads a `QString`-typed attr off `id`, or an empty string if absent /
/// wrong-typed.
QString stringAttr(MarkoffDocument *doc, BlockId id, const QByteArray &name)
{
    const auto attrs = doc->blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.cend())
        return {};
    const QString *v = std::get_if<QString>(&it.value());
    return v ? *v : QString();
}

/// Reads an `int`-typed attr off `id`, defaulting to `def` if absent /
/// wrong-typed. Mirrors `intAttr()` in `BlockPresentation.cpp`/`Folding.cpp`
/// (private to those files; duplicated here rather than shared across a
/// leaf-internal boundary for one three-line helper).
int intAttr(MarkoffDocument *doc, BlockId id, const QByteArray &name, int def)
{
    const auto attrs = doc->blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.cend())
        return def;
    const int *v = std::get_if<int>(&it.value());
    return v ? *v : def;
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
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return QAccessible::NoRole;

    switch (doc->blockKind(m_id)) {
    case BlockKind::Paragraph:
        // Footnote-definition paragraphs (`[^label]: ...`) are, per the
        // document model, completely ordinary Paragraph blocks (spec §4.2
        // *(footnote def)* row) — View::isFootnoteDefBlock() is the only
        // place that distinguishes them (presentation-layer detection over
        // the realized entry). No Qt role exists for AT-SPI's ROLE_FOOTNOTE
        // either, so Section is the same best-available choice as
        // BlockQuote below.
        return m_view->isFootnoteDefBlock(m_id) ? QAccessible::Section : QAccessible::Paragraph;
    case BlockKind::Heading:
        return QAccessible::Heading;
    case BlockKind::CodeBlock:
        // No dedicated "code" role in Qt's QAccessible::Role enum — the
        // language is carried in the description instead (spec §4.2).
        return QAccessible::EditableText;
    case BlockKind::ListItem:
        return QAccessible::ListItem;
    case BlockKind::BlockQuote:
        // LIMITATION (spec §4.6 finding 4): AT-SPI's ROLE_BLOCK_QUOTE
        // exists but no QAccessible::Role maps to it — Section is the best
        // available. Do not "fix" this by hunting for a better Qt role;
        // there isn't one as of Qt 6.11.
        return QAccessible::Section;
    case BlockKind::HorizontalRule:
        return QAccessible::Separator;
    case BlockKind::Image:
        return QAccessible::Graphic;
    case BlockKind::Math:
        // LIMITATION (spec §4.6 finding 4): AT-SPI's ROLE_MATH exists but
        // no QAccessible::Role maps to it — StaticText (→ ROLE_LABEL) is
        // the best available; the math source is exposed as the name.
        // Do not "fix" this by hunting for a better Qt role; there isn't
        // one as of Qt 6.11.
        return QAccessible::StaticText;
    case BlockKind::Mermaid:
        return QAccessible::Graphic;
    case BlockKind::HtmlBlock:
        // Raw HTML source is what the user actually edits here.
        return QAccessible::EditableText;
    case BlockKind::Table:
        // QAccessibleTableInterface is explicitly deferred (spec §6) — role
        // only; a screen reader announces "table" and reads it linearly.
        return QAccessible::Table;
    }
    return QAccessible::NoRole;
}

QAccessible::State CanvasBlockAccessible::state() const
{
    QAccessible::State s{};
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return s;

    s.focusable = true;
    s.focused = (m_view->caretBlock() == m_id);
    s.editable = !m_view->isReadOnly();
    s.invisible = m_view->isBlockHidden(m_id);

    if (doc->blockKind(m_id) == BlockKind::ListItem
        && stringAttr(doc, m_id, AttrNames::MarkerStyle) == QStringLiteral("task")) {
        s.checkable = true;
        s.checked = boolAttr(doc, m_id, AttrNames::Checked);
    }

    return s;
}

void *CanvasBlockAccessible::interface_cast(QAccessible::InterfaceType t)
{
    if (t == QAccessible::AttributesInterface)
        return static_cast<QAccessibleAttributesInterface *>(this);
    return nullptr;
}

QList<QAccessible::Attribute> CanvasBlockAccessible::attributeKeys() const
{
    if (role() == QAccessible::Heading)
        return {QAccessible::Attribute::Level};
    return {};
}

QVariant CanvasBlockAccessible::attributeValue(QAccessible::Attribute key) const
{
    if (key != QAccessible::Attribute::Level || role() != QAccessible::Heading)
        return {};
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return {};
    return QVariant(intAttr(doc, m_id, AttrNames::Level, 1));
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
