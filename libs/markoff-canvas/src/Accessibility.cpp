// SPDX-License-Identifier: GPL-3.0-or-later
#include "Accessibility.h"

#include <climits>
#include <variant>

#include <QCoreApplication>
#include <QInputMethodEvent>
#include <QScrollBar>
#include <QWidget>

#include <markoff/canvas/View.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/CrdtProxies.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/TextUnits.h>

#include "CodeHighlighting.h"
#include "MediaBlocks.h"

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

/// This block's byte sub-range covered by the view's current selection, or
/// `{-1, -1}` if `id` isn't selected at all (no selection active, or the
/// selection doesn't reach this block). Built entirely from `View`'s public
/// inspection surface — `hasSelection()`/`selectionAnchorBlock()`/
/// `selectionAnchorByteOffset()`/`caretBlock()`/`caretByteOffset()`/
/// `blockIndexOf()` — deliberately never the private `orderedSelection()`/
/// `selectedByteRangeInBlock()` pair `View.cpp` uses internally: A2.2 has no
/// spec-authorized reason to grow the public surface or add a friend, and
/// re-deriving this from already-public per-block-index comparisons is a
/// handful of lines, not a second coordinate space (C4 stays about
/// cross-block BYTE sums, never index comparisons — `CanvasAccessible`
/// itself already orders children by index the same way).
std::pair<int, int> blockSelectedByteRange(View *view, BlockId id)
{
    if (!view->hasSelection())
        return {-1, -1};

    const BlockId anchorBlock = view->selectionAnchorBlock();
    const int anchorByte = view->selectionAnchorByteOffset();
    const BlockId caretBlock = view->caretBlock();
    const int caretByte = view->caretByteOffset();

    const int anchorIndex = view->blockIndexOf(anchorBlock);
    const int caretIndex = view->blockIndexOf(caretBlock);
    if (anchorIndex < 0 || caretIndex < 0)
        return {-1, -1};

    // Order the two endpoints into document order (start <= end).
    BlockId startBlock = anchorBlock, endBlock = caretBlock;
    int startIndex = anchorIndex, endIndex = caretIndex;
    int startByte = anchorByte, endByte = caretByte;
    if (caretIndex < anchorIndex || (caretIndex == anchorIndex && caretByte < anchorByte)) {
        startBlock = caretBlock; endBlock = anchorBlock;
        startIndex = caretIndex; endIndex = anchorIndex;
        startByte = caretByte; endByte = anchorByte;
    }
    Q_UNUSED(startBlock);
    Q_UNUSED(endBlock);

    const int index = view->blockIndexOf(id);
    if (index < startIndex || index > endIndex)
        return {-1, -1};

    MarkoffDocument *doc = view->document();
    if (!doc)
        return {-1, -1};
    const int blockLen = doc->blockText(id).size();

    const int from = (index == startIndex) ? startByte : 0;
    const int to = (index == endIndex) ? endByte : blockLen;
    if (from >= to)
        return {-1, -1};
    return {from, to};
}

}  // namespace

// ---- CanvasAccessible ------------------------------------------------

namespace {
/// A3.3: cheap "did this block's buffer change" token. NO single core
/// counter covers every mutation path: `blockEditSequence` bumps on local
/// edits and undo/redo but NOT on remote CRDT ops; the block's
/// `BufferProxy::editSequence` bumps on local and remote edits but NOT on
/// undo/redo. Both are monotonic, so their sum changes on every path.
quint64 bufferChangeToken(MarkoffDocument *doc, BlockId id)
{
    quint64 t = doc->blockEditSequence(id);
    if (const auto *proxy = doc->bufferProxy(id))
        t += proxy->editSequence();
    return t;
}

// A3.3: View* -> its container, so View::onDocumentChanged() can reach an
// EXISTING container without QAccessible::queryAccessibleInterface() (which
// would create one for every View, AT client or not). Populated by the
// container ctor/dtor only; the dtor never touches the (possibly already
// destroyed) View.
std::unordered_map<const View *, CanvasAccessible *> &containerRegistry()
{
    // Leaked on purpose: Qt's accessible cache may destroy a container after
    // static destruction has begun.
    static auto *r = new std::unordered_map<const View *, CanvasAccessible *>;
    return *r;
}
}  // namespace

CanvasAccessible::CanvasAccessible(View *view)
    : QAccessibleWidget(view, QAccessible::Document)
    , m_view(view)
{
    containerRegistry()[view] = this;
    const int n = view->blockCount();
    m_knownIds.reserve(size_t(n));
    for (int i = 0; i < n; ++i)
        m_knownIds.push_back(view->blockIdAt(i));
}

CanvasAccessible::~CanvasAccessible()
{
    auto &reg = containerRegistry();
    if (auto it = reg.find(m_view); it != reg.end() && it->second == this)
        reg.erase(it);

    // Qt's cache owns the block accessibles (see m_children's comment);
    // deleteAccessibleInterface is a no-op for an id the cache already
    // dropped (e.g. cache-destructor teardown order).
    for (auto &[id, child] : m_children)
        QAccessible::deleteAccessibleInterface(child.id);
}

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

namespace {
// A4.1: fold-derived state bits, the unit of the last-announced diff.
constexpr quint8 kFoldExpandable = 1;
constexpr quint8 kFoldExpanded = 2;
constexpr quint8 kFoldInvisible = 4;

quint8 foldBitsFor(const View *view, BlockId id)
{
    quint8 bits = 0;
    if (view->isBlockFoldable(id)) {
        bits |= kFoldExpandable;
        // `expanded` = the fold head's body is visible = NOT folded.
        if (!view->isBlockFolded(id))
            bits |= kFoldExpanded;
    }
    if (view->isBlockHidden(id))
        bits |= kFoldInvisible;
    return bits;
}
}  // namespace

CanvasBlockAccessible *CanvasAccessible::blockAccessible(BlockId id) const
{
    if (id.isNull())
        return nullptr;
    auto it = m_children.find(id);
    if (it != m_children.end())
        return it->second.iface;
    // Only ids in the current document get an accessible: a stale id (a
    // removed block a caller still holds) must never be resurrected into
    // m_children, where nothing would ever evict it.
    if (m_view->blockIndexOf(id) < 0)
        return nullptr;
    auto *block = new CanvasBlockAccessible(m_view, const_cast<CanvasAccessible *>(this), id);
    const QAccessible::Id qid = QAccessible::registerAccessibleInterface(block);
    Child c{block, qid, 0, {}, foldBitsFor(m_view, id)};
    if (MarkoffDocument *doc = m_view->document()) {
        c.seq = bufferChangeToken(doc, id);
        c.text = QString::fromUtf8(doc->blockText(id));
    }
    m_children.emplace(id, std::move(c));
    return block;
}

// ---- A3.2 notifications (spec §4.4) ------------------------------------

CanvasAccessible::SelSnapshot CanvasAccessible::currentSelection() const
{
    SelSnapshot s;
    if (!m_view->hasSelection())
        return s;
    const BlockId ab = m_view->selectionAnchorBlock();
    const int aByte = m_view->selectionAnchorByteOffset();
    const BlockId cb = m_view->caretBlock();
    const int cByte = m_view->caretByteOffset();
    const int ai = m_view->blockIndexOf(ab);
    const int ci = m_view->blockIndexOf(cb);
    if (ai < 0 || ci < 0)
        return s;
    const bool caretFirst = ci < ai || (ci == ai && cByte < aByte);
    s.valid = true;
    s.startBlock = caretFirst ? cb : ab;
    s.startByte = caretFirst ? cByte : aByte;
    s.endBlock = caretFirst ? ab : cb;
    s.endByte = caretFirst ? aByte : cByte;
    return s;
}

namespace {

/// Per-block byte intersection of an ordered selection, {-1,-1} if none.
/// Same semantics as blockSelectedByteRange() (A2.2), but over a snapshot so
/// the OLD selection can be compared against the new one. Indices are
/// block-index comparisons only (C4: never cross-block byte sums).
std::pair<int, int> intersectSel(bool valid, int startIdx, int startByte, int endIdx, int endByte,
                                 int index, int blockLen)
{
    if (!valid || index < startIdx || index > endIdx)
        return {-1, -1};
    const int from = (index == startIdx) ? startByte : 0;
    const int to = (index == endIdx) ? endByte : blockLen;
    if (from >= to)
        return {-1, -1};
    return {from, to};
}

}  // namespace

void CanvasAccessible::syncTextNotifications(bool viewHasFocus)
{
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return;

    const BlockId caretBlock = m_view->caretBlock();
    const int caretByte = m_view->caretByteOffset();
    const bool blockMoved = caretBlock != m_notifiedCaretBlock;
    const bool caretMoved = blockMoved || caretByte != m_notifiedCaretByte;
    const SelSnapshot old = m_notifiedSel;
    const SelSnapshot now = currentSelection();
    m_notifiedCaretBlock = caretBlock;
    m_notifiedCaretByte = caretByte;
    m_notifiedSel = now;

    // Caret. Decision (A3.2): notify only the block that NOW holds the
    // caret. The old block is not sent a "cursor lost" event — its
    // cursorPosition() simply turns -1, AT-SPI clients track the caret via
    // the new holder's event, and the Focus event below moves the a11y
    // focus. (An event with position -1 is not a defined AT-SPI shape.)
    if (!caretBlock.isNull()) {
        CanvasBlockAccessible *b = blockAccessible(caretBlock);
        if (b) {
            if (blockMoved && viewHasFocus) {
                QAccessibleEvent focus(b, QAccessible::Focus);
                QAccessible::updateAccessibility(&focus);
            }
            if (caretMoved && b->interface_cast(QAccessible::TextInterface)) {
                const QByteArray raw = doc->blockText(caretBlock);
                QAccessibleTextCursorEvent ev(b, int(coords::byteToQtPos(raw, caretByte)));
                QAccessible::updateAccessibility(&ev);
            }
        }
    }

    // Selection: one event per block whose per-block intersection changed,
    // including blocks whose intersection shrank to empty (event with
    // (-1,-1) — how Qt's QAccessibleTextSelectionEvent spells "cleared").
    const int oldSi = old.valid ? m_view->blockIndexOf(old.startBlock) : -1;
    const int oldEi = old.valid ? m_view->blockIndexOf(old.endBlock) : -1;
    const int newSi = now.valid ? m_view->blockIndexOf(now.startBlock) : -1;
    const int newEi = now.valid ? m_view->blockIndexOf(now.endBlock) : -1;
    const bool oldOk = old.valid && oldSi >= 0 && oldEi >= 0;
    const bool newOk = now.valid && newSi >= 0 && newEi >= 0;
    if (!oldOk && !newOk)
        return;

    int lo = INT_MAX, hi = -1;
    if (oldOk) { lo = qMin(lo, oldSi); hi = qMax(hi, oldEi); }
    if (newOk) { lo = qMin(lo, newSi); hi = qMax(hi, newEi); }

    for (int i = lo; i <= hi; ++i) {
        const bool endpoint = (oldOk && (i == oldSi || i == oldEi)) || (newOk && (i == newSi || i == newEi));
        if (!endpoint) {
            // Strict interior of either range = fully selected. Cheap
            // membership compare, no buffer read, so a huge
            // select-all-then-extend stays O(span) integer compares.
            const bool oldIn = oldOk && i > oldSi && i < oldEi;
            const bool newIn = newOk && i > newSi && i < newEi;
            if (oldIn == newIn)
                continue;
        }
        const BlockId id = m_view->blockIdAt(i);
        if (id.isNull())
            continue;
        CanvasBlockAccessible *b = blockAccessible(id);
        if (!b || !b->interface_cast(QAccessible::TextInterface))
            continue;
        const QByteArray raw = doc->blockText(id);
        const int len = raw.size();
        const auto o = intersectSel(oldOk, oldSi, old.startByte, oldEi, old.endByte, i, len);
        const auto n = intersectSel(newOk, newSi, now.startByte, newEi, now.endByte, i, len);
        if (o == n)
            continue;
        int s = -1, e = -1;
        if (n.first >= 0) {
            s = int(coords::byteToQtPos(raw, n.first));
            e = int(coords::byteToQtPos(raw, n.second));
        }
        QAccessibleTextSelectionEvent ev(b, s, e);
        QAccessible::updateAccessibility(&ev);
    }
}

void CanvasAccessible::notifyFocusChange(bool gained)
{
    CanvasBlockAccessible *b = blockAccessible(m_view->caretBlock());
    if (gained) {
        if (b) {
            QAccessibleEvent ev(b, QAccessible::Focus);
            QAccessible::updateAccessibility(&ev);
        } else {
            QAccessibleEvent ev(m_view, QAccessible::Focus);
            QAccessible::updateAccessibility(&ev);
        }
        return;
    }
    QAccessible::State changed;
    changed.focused = true;
    if (b) {
        QAccessibleStateChangeEvent ev(b, changed);
        QAccessible::updateAccessibility(&ev);
    } else {
        QAccessibleStateChangeEvent ev(m_view, changed);
        QAccessible::updateAccessibility(&ev);
    }
}

// ---- A3.3 text + structure events (spec §4.4, §4.5, §9 Q1) ---------------

void CanvasAccessible::syncStructure()
{
    MarkoffDocument *doc = m_view->document();
    const bool active = QAccessible::isActive();

    // 1. Text: only created blocks whose edit sequence moved. Per block, per
    // QChar (C4) — the buffer is read whole and diffed against this block's
    // own snapshot, never against a cross-block offset.
    if (doc) {
        for (auto &[id, child] : m_children) {
            if (m_view->blockIndexOf(id) < 0)
                continue;  // being removed; evicted in step 3
            const quint64 seq = bufferChangeToken(doc, id);
            if (seq == child.seq)
                continue;
            child.seq = seq;
            const QString now = QString::fromUtf8(doc->blockText(id));
            if (now == child.text)
                continue;
            const QString old = std::move(child.text);
            child.text = now;
            if (!active || !child.iface->interface_cast(QAccessible::TextInterface))
                continue;
            // Common prefix / suffix, never splitting a surrogate pair.
            const int minLen = qMin(old.size(), now.size());
            int pre = 0;
            while (pre < minLen && old.at(pre) == now.at(pre))
                ++pre;
            if (pre > 0 && ((pre < old.size() && old.at(pre).isLowSurrogate())
                            || (pre < now.size() && now.at(pre).isLowSurrogate())))
                --pre;
            int suf = 0;
            while (suf < minLen - pre
                   && old.at(old.size() - 1 - suf) == now.at(now.size() - 1 - suf))
                ++suf;
            if (suf > 0 && old.at(old.size() - suf).isLowSurrogate())
                --suf;
            const QString removed = old.mid(pre, old.size() - pre - suf);
            const QString inserted = now.mid(pre, now.size() - pre - suf);
            if (!removed.isEmpty()) {
                QAccessibleTextRemoveEvent ev(child.iface, pre, removed);
                QAccessible::updateAccessibility(&ev);
            }
            if (!inserted.isEmpty()) {
                QAccessibleTextInsertEvent ev(child.iface, pre, inserted);
                QAccessible::updateAccessibility(&ev);
            }
        }
    }

    // 2. New blocks. Diff the id list against the last-seen one; typing
    // (same list) is a straight elementwise compare, no set built.
    const int n = m_view->blockCount();
    bool same = size_t(n) == m_knownIds.size();
    for (int i = 0; same && i < n; ++i)
        same = m_knownIds[size_t(i)] == m_view->blockIdAt(i);
    if (!same) {
        std::unordered_map<BlockId, char, BlockIdHash> oldSet;
        oldSet.reserve(m_knownIds.size());
        for (BlockId id : m_knownIds)
            oldSet.emplace(id, 0);
        std::vector<BlockId> fresh;
        fresh.reserve(size_t(n));
        for (int i = 0; i < n; ++i) {
            const BlockId id = m_view->blockIdAt(i);
            fresh.push_back(id);
            if (active && !m_primeSilently && !id.isNull() && oldSet.find(id) == oldSet.end()) {
                if (CanvasBlockAccessible *b = blockAccessible(id)) {
                    QAccessibleEvent ev(b, QAccessible::ObjectCreated);
                    QAccessible::updateAccessibility(&ev);
                }
            }
        }
        m_knownIds = std::move(fresh);
    }
    m_primeSilently = false;

    // 3. Eviction (spec §9 Q1): every created block that left the document.
    std::vector<BlockId> gone;
    for (const auto &[id, child] : m_children)
        if (m_view->blockIndexOf(id) < 0)
            gone.push_back(id);
    for (BlockId id : gone)
        evict(id);
}

void CanvasAccessible::syncFoldNotifications()
{
    if (!QAccessible::isActive())
        return;
    for (auto &[id, child] : m_children) {
        if (m_view->blockIndexOf(id) < 0)
            continue;  // leaving the document; syncStructure() evicts it
        const quint8 now = foldBitsFor(m_view, id);
        const quint8 diff = now ^ child.foldBits;
        if (!diff)
            continue;
        child.foldBits = now;
        // One event per flag: the AT-SPI bridge handles only one changed
        // flag per StateChange event (else-if chain in atspiadaptor.cpp).
        auto emitFlag = [&](auto setter) {
            QAccessible::State changed;
            setter(changed);
            QAccessibleStateChangeEvent ev(child.iface, changed);
            QAccessible::updateAccessibility(&ev);
        };
        if (diff & kFoldExpandable)
            emitFlag([](QAccessible::State &s) { s.expandable = true; });
        if (diff & kFoldExpanded)
            emitFlag([](QAccessible::State &s) { s.expanded = true; });
        if (diff & kFoldInvisible)
            emitFlag([](QAccessible::State &s) { s.invisible = true; });
    }
}

void CanvasAccessible::notifyReadOnlyChanged()
{
    if (!QAccessible::isActive())
        return;
    // Spec §4.4 "read-only flipped": `state().editable` on the container and
    // on every block (CanvasBlockAccessible::state() derives it from
    // View::isReadOnly()). Only CREATED blocks are announced - an AT client
    // can only hold references to blocks it has queried.
    QAccessible::State changed;
    changed.editable = true;
    QAccessibleStateChangeEvent containerEv(m_view, changed);
    QAccessible::updateAccessibility(&containerEv);
    for (auto &[id, child] : m_children) {
        if (m_view->blockIndexOf(id) < 0)
            continue;  // leaving the document; syncStructure() evicts it
        QAccessibleStateChangeEvent ev(child.iface, changed);
        QAccessible::updateAccessibility(&ev);
    }
}

void CanvasAccessible::evict(BlockId id)
{
    auto it = m_children.find(id);
    if (it == m_children.end())
        return;
    const Child child = it->second;
    m_children.erase(it);  // unreachable from child()/indexOfChild() from here on
    // Qt itself emits the ObjectDestroyed for a cached interface (while
    // active) from deleteAccessibleInterface; emitting our own too would
    // double-announce it (observed in A3.3: 2 events per removal).
    QAccessible::deleteAccessibleInterface(child.id);
}

void CanvasAccessible::resetForNewDocument()
{
    std::vector<BlockId> all;
    all.reserve(m_children.size());
    for (const auto &[id, child] : m_children)
        all.push_back(id);
    for (BlockId id : all)
        evict(id);
    m_knownIds.clear();
    m_primeSilently = true;
    m_notifiedCaretBlock = {};
    m_notifiedCaretByte = -1;
    m_notifiedSel = {};
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

QString CanvasBlockAccessible::text(QAccessible::Text t) const
{
    // Spec §4.2 per-kind notes + §6 (A5.2 audit: this was an empty stub).
    // Block CONTENT is the text interface's job; Name/Description carry only
    // what the role alone cannot say. Text/paragraph/heading/list blocks
    // deliberately have neither (the content is the name).
    MarkoffDocument *doc = m_view->document();
    if (!doc || (t != QAccessible::Name && t != QAccessible::Description))
        return {};
    const bool name = (t == QAccessible::Name);

    switch (doc->blockKind(m_id)) {
    case BlockKind::Image: {
        if (!name)
            return {};
        // View::mediaLabelFor() is layout-derived (empty until the block has
        // been realized); parse the buffer instead when it is empty so the
        // answer never depends on viewport position and never realizes (§5).
        const QString label = m_view->mediaLabelFor(m_id);
        if (!label.isEmpty())
            return label;
        const Detail::ImageBlockInfo info = Detail::parseImageBlock(doc->blockText(m_id));
        const QString display = info.altOrAlias.isEmpty() ? info.target : info.altOrAlias;
        return display.isEmpty() ? m_view->tr("Image") : display;
    }
    case BlockKind::Mermaid:
        return name ? m_view->tr("Mermaid diagram") : QString();
    case BlockKind::Math:
        // Role is StaticText (no Qt math role, spec §4.6 finding 4), so the
        // TeX source is the name.
        return {};
    case BlockKind::CodeBlock: {
        // No code role in Qt: the fence language goes in the description.
        if (name)
            return {};
        const QString lang = Detail::parseCodeFence(doc->blockText(m_id)).language;
        return lang.isEmpty() ? QString() : m_view->tr("Code, language %1").arg(lang);
    }
    case BlockKind::Table: {
        // §6: role Table + usable name/description (no table interface).
        if (name)
            return m_view->tr("Table");
        // Rows = non-empty lines minus the delimiter row.
        int lines = 0;
        for (const QByteArray &l : doc->blockText(m_id).split('\n'))
            lines += l.trimmed().isEmpty() ? 0 : 1;
        return m_view->tr("%n row(s)", nullptr, qMax(0, lines - 1));
    }
    default:
        return {};
    }
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
    // A4.1 (spec §4.3): a fold head is expandable; expanded = body visible
    // (NOT folded), collapsed = folded. The H-arc hidden title is never
    // foldable (isBlockFoldable is false for it) so it is never expandable,
    // only invisible (it rides the fold-hidden projection).
    if (m_view->isBlockFoldable(m_id)) {
        s.expandable = true;
        const bool folded = m_view->isBlockFolded(m_id);
        s.expanded = !folded;
        s.collapsed = folded;
    }

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
    if (t == QAccessible::EditableTextInterface && hasTextContent())
        return static_cast<QAccessibleEditableTextInterface *>(this);
    if (t == QAccessible::ActionInterface && m_view->isBlockFoldable(m_id))
        return static_cast<QAccessibleActionInterface *>(this);
    return nullptr;
}

// ---- A4.2 editable text -------------------------------------------------
// Routing decision: an AT edit is at an arbitrary (block, offset), while
// View's edit helpers act on the caret. The clean route needing NO new View
// API is the IME-commit path: (1) place the caret at the range start with
// the public setCaretPosition(), (2) deliver a QInputMethodEvent whose
// replacementStart/Length + commitString describe the replace. View::
// inputMethodEvent already does exactly "one d2ApplyBufferEdit in one
// UndoLog::Transaction at the caret, then flush so kind promotion sees the
// text", owns the read-only gate, and converts QChar->byte with coords::
// inside its own block. No buffer is touched here, no logic duplicated.
// Consequences (deliberate): the caret ends after the inserted text (what
// Qt's own editable widgets do); the edit is one undo step; auto-pairing
// (a key-typing feature) does not apply; an in-flight preedit is cancelled.

void CanvasBlockAccessible::replaceRange(int startOffset, int endOffset, const QString &text)
{
    MarkoffDocument *doc = m_view->document();
    if (!doc || m_view->isReadOnly() || !hasTextContent())
        return;
    const QByteArray raw = doc->blockText(m_id);
    const int count = int(coords::byteToQtPos(raw, raw.size()));
    if (startOffset < 0 || endOffset < startOffset || endOffset > count)
        return;  // out of range / inverted: reject, never guess
    if (startOffset == endOffset && text.isEmpty())
        return;
    // A block is one paragraph: a line break in the payload would land as a
    // raw newline in a non-code block's buffer (paste/typing split blocks
    // through the structural-key path instead). Only code fences hold them.
    if (doc->blockKind(m_id) != BlockKind::CodeBlock
        && (text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))))
        return;

    const int startByte = int(coords::qtPosToByte(raw, startOffset));
    m_view->setCaretPosition(m_id, startByte);
    if (m_view->caretBlock() != m_id || m_view->caretByteOffset() != startByte)
        return;  // caret could not land here (e.g. redirected): don't edit elsewhere

    QInputMethodEvent ev(QString(), {});
    ev.setCommitString(text, 0, endOffset - startOffset);
    QCoreApplication::sendEvent(m_view, &ev);
}

void CanvasBlockAccessible::deleteText(int startOffset, int endOffset)
{
    replaceRange(startOffset, endOffset, QString());
}

void CanvasBlockAccessible::insertText(int offset, const QString &text)
{
    replaceRange(offset, offset, text);
}

void CanvasBlockAccessible::replaceText(int startOffset, int endOffset, const QString &text)
{
    replaceRange(startOffset, endOffset, text);
}

// ---- A4.1 actions -------------------------------------------------------
// Action naming: Qt's stock `toggleAction()` ("Toggle") — the same action
// Qt's own tree-item accessibles use for expand/collapse-like state flips.
// The AT-SPI bridge forwards Qt action names verbatim as the
// org.a11y.atspi.Action name, and there is no Qt-stock expand/collapse
// action, so a custom name would only be an unrecognized string to Orca.
// Fold is VIEW state, not a document mutation, so read-only mode does not
// block it (View::toggleFold has no read-only gate either).

QStringList CanvasBlockAccessible::actionNames() const
{
    if (!m_view->isBlockFoldable(m_id))
        return {};
    return {QAccessibleActionInterface::toggleAction()};
}

QString CanvasBlockAccessible::localizedActionName(const QString &name) const
{
    if (name == QAccessibleActionInterface::toggleAction())
        return m_view->tr("Toggle fold");
    return QAccessibleActionInterface::localizedActionName(name);
}

QString CanvasBlockAccessible::localizedActionDescription(const QString &name) const
{
    if (name == QAccessibleActionInterface::toggleAction()) {
        return m_view->isBlockFolded(m_id) ? m_view->tr("Expands this section")
                                           : m_view->tr("Collapses this section");
    }
    return QAccessibleActionInterface::localizedActionDescription(name);
}

void CanvasBlockAccessible::doAction(const QString &actionName)
{
    if (actionName == QAccessibleActionInterface::toggleAction())
        m_view->toggleFold(m_id);  // no-op if no longer foldable
}

QStringList CanvasBlockAccessible::keyBindingsForAction(const QString &) const
{
    return {};
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
    // QChar count, NOT byte count (A2.1 done-when) — coords::byteToQtPos
    // walking the full buffer is exactly that.
    return int(coords::byteToQtPos(raw, raw.size()));
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
    if (boundaryType == QAccessible::LineBoundary) {
        // A2.3: the actual WRAPPED visual line (View::lineByteRangeAt(),
        // which needs a realized QTextLayout — the block realizes here),
        // never the base class's literal-'\n'-splitting default (wrong for
        // a wrapped paragraph, which has no embedded '\n' at all).
        // textBeforeOffset/textAfterOffset for LineBoundary are NOT
        // overridden here — out of this task's named scope (plan: "…and
        // textAtOffset(…, LineBoundary)" only) — so they still fall
        // through to the base class's '\n'-based approximation. Logged,
        // not an oversight.
        if (!hasTextContent() || !m_view->ensureBlockRealized(m_id)) {
            *startOffset = *endOffset = -1;
            return {};
        }
        MarkoffDocument *doc = m_view->document();
        if (!doc) {
            *startOffset = *endOffset = -1;
            return {};
        }
        const QByteArray raw = doc->blockText(m_id);
        const int count = int(coords::byteToQtPos(raw, raw.size()));
        const int at = (offset == -1) ? count : offset;
        if (at < 0 || at > count) {
            *startOffset = *endOffset = -1;
            return {};
        }
        // Qt's own base-class convention (qaccessible.cpp's textLineBoundary):
        // querying exactly at the end asks about the line ending there, so
        // clamp into the last valid character rather than one past it.
        const int clampedAt = count > 0 ? qMin(at, count - 1) : 0;
        const int byteOffset = int(coords::qtPosToByte(raw, clampedAt));
        const auto [fromByte, toByte] = m_view->lineByteRangeAt(m_id, byteOffset);
        if (fromByte < 0) {
            *startOffset = *endOffset = -1;
            return {};
        }
        *startOffset = int(coords::byteToQtPos(raw, fromByte));
        *endOffset = int(coords::byteToQtPos(raw, toByte));
        return text(*startOffset, *endOffset);
    }
    return QAccessibleTextInterface::textAtOffset(offset, boundaryType, startOffset, endOffset);
}

void CanvasBlockAccessible::selection(int selectionIndex, int *startOffset, int *endOffset) const
{
    // A2.2: this block's intersection with the view's (single, anchor+caret)
    // selection — spec §4.1's "cross-block selections appear as a selection
    // on each spanned block". There is exactly one selection to report
    // (index 0); View has no multi-range selection model.
    if (selectionIndex != 0) {
        *startOffset = *endOffset = -1;
        return;
    }
    const auto [fromByte, toByte] = blockSelectedByteRange(m_view, m_id);
    if (fromByte < 0) {
        *startOffset = *endOffset = -1;
        return;
    }
    MarkoffDocument *doc = m_view->document();
    const QByteArray raw = doc ? doc->blockText(m_id) : QByteArray();
    *startOffset = int(coords::byteToQtPos(raw, fromByte));
    *endOffset = int(coords::byteToQtPos(raw, toByte));
}

int CanvasBlockAccessible::selectionCount() const
{
    // A2.2: 0 or 1 — same "one selection, possibly spanning this block or
    // not" model as selection() above.
    return blockSelectedByteRange(m_view, m_id).first >= 0 ? 1 : 0;
}

void CanvasBlockAccessible::addSelection(int, int)
{
    // Not implemented: `View` has no public API to programmatically set a
    // selection (its selection model is edited only through real input
    // events — mouse drag, Shift+move, Ctrl+A — same rule the header's own
    // "Selection (T5)" comment states for the caret). A2.2's own done-when
    // only requires cursorPosition()/selectionCount()/selection()/
    // setCursorPosition() to work; it does not claim addSelection/
    // removeSelection/setSelection. Adding a View setter to unblock these
    // would grow the public API beyond spec §9 Q2's one authorized
    // exception (accessibleDocumentName) — logged as a decision, not an
    // oversight (plan findings log, A2.2).
}

void CanvasBlockAccessible::removeSelection(int)
{
    // See addSelection() above — same reasoning, no View setter to route
    // through.
}

void CanvasBlockAccessible::setSelection(int, int, int)
{
    // See addSelection() above — same reasoning, no View setter to route
    // through.
}

int CanvasBlockAccessible::cursorPosition() const
{
    // A2.2: only the block currently holding the caret reports a position.
    if (m_view->caretBlock() != m_id)
        return -1;
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return -1;
    const QByteArray raw = doc->blockText(m_id);
    return int(coords::byteToQtPos(raw, m_view->caretByteOffset()));
}

void CanvasBlockAccessible::setCursorPosition(int position)
{
    // A2.2: routes to View::setCaretPosition() — the one real caret-moving
    // chokepoint (spec §4.1).
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return;
    const QByteArray raw = doc->blockText(m_id);
    const int clamped = qBound(0, position, int(coords::byteToQtPos(raw, raw.size())));
    m_view->setCaretPosition(m_id, int(coords::qtPosToByte(raw, clamped)));
}

QRect CanvasBlockAccessible::characterRect(int offset) const
{
    // A2.3: needs a QTextLayout — the one path allowed to force realization
    // of the queried block (spec §5), via View::ensureBlockRealized().
    if (!hasTextContent())
        return {};
    if (!m_view->ensureBlockRealized(m_id))
        return {};
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return {};
    const QByteArray raw = doc->blockText(m_id);
    const int count = int(coords::byteToQtPos(raw, raw.size()));
    if (offset < 0 || offset > count)
        return {};
    const int byteOffset = int(coords::qtPosToByte(raw, offset));
    const QRect viewportRect = m_view->characterRectInViewport(m_id, byteOffset);
    if (viewportRect.isNull())
        return {};
    // Global screen coordinates, same convention rect() already uses
    // (blockGlobalRect() above) — QAccessibleTextInterface::characterRect's
    // contract.
    return QRect(m_view->viewport()->mapToGlobal(viewportRect.topLeft()), viewportRect.size());
}

int CanvasBlockAccessible::offsetAtPoint(const QPoint &point) const
{
    // A2.3: `point` is in global screen coordinates (same convention
    // characterRect() above returns). Reject a point outside this block's
    // own rect first — a block's text interface only ever answers for its
    // own buffer/geometry (C4's per-block discipline extended to the
    // geometry surface) — then force realization of this block, otherwise
    // there is no layout to measure a column against.
    if (!hasTextContent())
        return -1;
    if (!rect().contains(point))
        return -1;
    if (!m_view->ensureBlockRealized(m_id))
        return -1;
    const QPoint viewportPoint = m_view->viewport()->mapFromGlobal(point);
    const int byteOffset = m_view->byteOffsetAtPoint(m_id, viewportPoint);
    if (byteOffset < 0)
        return -1;
    MarkoffDocument *doc = m_view->document();
    if (!doc)
        return -1;
    const QByteArray raw = doc->blockText(m_id);
    return int(coords::byteToQtPos(raw, byteOffset));
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

namespace {
CanvasAccessible *containerFor(View *view)
{
    return dynamic_cast<CanvasAccessible *>(QAccessible::queryAccessibleInterface(view));
}
}  // namespace

void notifyTextState(View *view, bool viewHasFocus)
{
    // Spec §4.4: isActive() short-circuit — nothing runs, nothing is
    // allocated, when no AT client is attached.
    if (!QAccessible::isActive())
        return;
    if (CanvasAccessible *c = containerFor(view))
        c->syncTextNotifications(viewHasFocus);
}

void notifyDocumentChanged(View *view)
{
    auto &reg = containerRegistry();
    if (reg.empty())
        return;
    if (auto it = reg.find(view); it != reg.end())
        it->second->syncStructure();
}

void notifyDocumentReplaced(View *view)
{
    auto &reg = containerRegistry();
    if (auto it = reg.find(view); it != reg.end())
        it->second->resetForNewDocument();
}

void notifyFoldState(View *view)
{
    if (!QAccessible::isActive())
        return;
    auto &reg = containerRegistry();
    if (auto it = reg.find(view); it != reg.end())
        it->second->syncFoldNotifications();
}

void notifyReadOnlyChanged(View *view)
{
    if (!QAccessible::isActive())
        return;
    auto &reg = containerRegistry();
    if (auto it = reg.find(view); it != reg.end())
        it->second->notifyReadOnlyChanged();
}

void notifyFocusChange(View *view, bool gained)
{
    if (!QAccessible::isActive())
        return;
    if (CanvasAccessible *c = containerFor(view))
        c->notifyFocusChange(gained);
}

}  // namespace Markoff::Canvas::Detail
