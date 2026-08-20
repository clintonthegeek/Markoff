// SPDX-License-Identifier: GPL-3.0-or-later
#include "Accessibility.h"

#include <variant>

#include <QScrollBar>
#include <QWidget>

#include <markoff/canvas/View.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/TextUnits.h>

namespace coords = Markoff::TextUnits;

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

QString CanvasAccessible::text(QAccessible::Text t) const
{
    if (t == QAccessible::Name) {
        // Resolution order per spec §9 Q2 / A1.3: explicit embedder-set name
        // -> the (also embedder-set, but rendered) inline title -> a generic
        // fallback. `tr()` goes through `m_view` — this class isn't a
        // QObject (QAccessibleWidget wraps one, isn't one), so it has no
        // translation context of its own; reusing View's is the same choice
        // the "Untitled" placeholder inside View itself already makes.
        if (!m_view->accessibleDocumentName().isEmpty())
            return m_view->accessibleDocumentName();
        if (!m_view->inlineTitle().isEmpty())
            return m_view->inlineTitle();
        return m_view->tr("Markdown document");
    }
    return QAccessibleWidget::text(t);
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
    if (t == QAccessible::TextInterface && hasTextContent())
        return static_cast<QAccessibleTextInterface *>(this);
    return nullptr;
}

bool CanvasBlockAccessible::hasTextContent() const
{
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return false;
    // Spec §4.2: these three kinds have no text interface at all.
    switch (doc->blockKind(m_id)) {
    case BlockKind::HorizontalRule:
    case BlockKind::Image:
    case BlockKind::Mermaid:
        return false;
    default:
        return true;
    }
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

// ---- CanvasBlockAccessible :: QAccessibleTextInterface (A2.1) ----------
//
// All offsets below are QChar (UTF-16 code-unit) indices into this block's
// own `document()->blockText(m_id)` buffer, converted from/to UTF-8 byte
// offsets with `coords::` — the same helpers `View.cpp` already uses
// everywhere it straddles the two text units. Never cross-block (C4): a
// block's text interface only ever sees its own buffer.

QString CanvasBlockAccessible::text(int startOffset, int endOffset) const
{
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return {};
    const QByteArray raw = doc->blockText(m_id);
    const int count = int(coords::byteToQtPos(raw, raw.size()));

    if (startOffset < 0)
        startOffset = 0;
    if (endOffset < 0 || endOffset > count)
        endOffset = count;
    if (startOffset >= endOffset)
        return {};

    const qsizetype startByte = coords::qtPosToByte(raw, startOffset);
    const qsizetype endByte = coords::qtPosToByte(raw, endOffset);
    return QString::fromUtf8(raw.mid(startByte, endByte - startByte));
}

int CanvasBlockAccessible::characterCount() const
{
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return 0;
    const QByteArray raw = doc->blockText(m_id);
    // BREAK (falsification, throwaway): byte count instead of QChar count.
    return int(raw.size());
}

QString CanvasBlockAccessible::textBeforeOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                                                 int *startOffset, int *endOffset) const
{
    // A block is exactly one paragraph (spec §4.2/A2.1's task description):
    // there is no *previous* paragraph within this block's own buffer, so
    // "the paragraph before offset" is always "no such item" here — the
    // base class's line-break-search default would instead go hunting for
    // an embedded '\n' (wrong for e.g. a CodeBlock buffer, which keeps its
    // fence + interior newlines inline per the buffer-convention table).
    if (boundaryType == QAccessible::ParagraphBoundary) {
        *startOffset = *endOffset = -1;
        return {};
    }
    // CharBoundary/WordBoundary: the base implementation already operates
    // purely in QChar space via this class's own text()/characterCount(),
    // so no override is needed for those.
    return QAccessibleTextInterface::textBeforeOffset(offset, boundaryType, startOffset, endOffset);
}

QString CanvasBlockAccessible::textAfterOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                                                int *startOffset, int *endOffset) const
{
    // Symmetric with textBeforeOffset() above: no *next* paragraph within
    // this block either.
    if (boundaryType == QAccessible::ParagraphBoundary) {
        *startOffset = *endOffset = -1;
        return {};
    }
    return QAccessibleTextInterface::textAfterOffset(offset, boundaryType, startOffset, endOffset);
}

QString CanvasBlockAccessible::textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                                             int *startOffset, int *endOffset) const
{
    if (boundaryType == QAccessible::ParagraphBoundary) {
        // A block IS a paragraph — the paragraph at any in-range offset is
        // the whole block, full stop (spec §4.1, A2.1 task description).
        const int count = characterCount();
        const int at = (offset == -1) ? count : offset;
        if (at < 0 || at > count) {
            *startOffset = *endOffset = -1;
            return {};
        }
        *startOffset = 0;
        *endOffset = count;
        return text(0, count);
    }
    return QAccessibleTextInterface::textAtOffset(offset, boundaryType, startOffset, endOffset);
}

void CanvasBlockAccessible::selection(int, int *startOffset, int *endOffset) const
{
    // A2.2's job (caret + selection, cross-block presentation, spec §4.1).
    // Placeholder: this block never reports a selection yet.
    *startOffset = *endOffset = -1;
}

int CanvasBlockAccessible::selectionCount() const
{
    // A2.2 placeholder.
    return 0;
}

void CanvasBlockAccessible::addSelection(int, int)
{
    // A2.2 placeholder — no-op until selection routes through View.
}

void CanvasBlockAccessible::removeSelection(int)
{
    // A2.2 placeholder — no-op.
}

void CanvasBlockAccessible::setSelection(int, int, int)
{
    // A2.2 placeholder — no-op.
}

int CanvasBlockAccessible::cursorPosition() const
{
    // A2.2's job (`View::caretByteOffset()` converted, only when
    // `View::caretBlock() == id`). Placeholder: no caret reported yet.
    return -1;
}

void CanvasBlockAccessible::setCursorPosition(int)
{
    // A2.2 placeholder — no-op until routed to `View::setCaretPosition()`.
}

QRect CanvasBlockAccessible::characterRect(int) const
{
    // A2.3's job (needs a QTextLayout, spec §5 — geometry queries are the
    // one path allowed to force realization). Placeholder: null rect.
    return {};
}

int CanvasBlockAccessible::offsetAtPoint(const QPoint &) const
{
    // A2.3 placeholder.
    return -1;
}

void CanvasBlockAccessible::scrollToSubstring(int, int)
{
    // Not claimed by any task yet; a no-op is safe here (nothing currently
    // calls it, and there's no scroll-to-block-substring seam to route
    // through until one is needed).
}

QString CanvasBlockAccessible::attributes(int offset, int *startOffset, int *endOffset) const
{
    // Not claimed by any task yet. Following A1.1's "compile-complete but
    // explicitly-placeholder" precedent: no text attribute runs are
    // reported, and the queried offset is echoed back as a zero-length
    // range (rather than -1/-1) since `offset` itself is a valid position,
    // just one with no attribute run info to report.
    *startOffset = *endOffset = offset;
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
