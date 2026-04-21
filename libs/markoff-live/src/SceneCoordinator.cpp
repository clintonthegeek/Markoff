// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneCoordinator.h"
#include "SelectionScene.h"
#include "SelectableItem.h"
#include "MarkdownTextItem.h"
#include "TextControl.h"
#include "ImageBlockItem.h"
#include "FoldingModel.h"
#include "MathTextObject.h"
#include "TableSerializer.h"
#include <markoff-parser/MarkdownSplitter.h>
#include <markoff-parser/TableHandler.h>
#include "MarkdownHighlighter.h"
#include <markoff-parser/TreeSitterParser.h>
#include <markoff/MarkdownDelta.h>

#include <QTimer>
#include <QTextDocument>
#include <QTextTable>
#include <QTextFrame>
#include <QGuiApplication>
#include <QUndoStack>
#include <QPointer>

namespace Markoff {

namespace {
/// Count newlines in `utf8` up to and including byte offset `byteOffset`.
/// The returned line index is 0-based: byteOffset 0 → line 0, byteOffset
/// at the first byte after the first '\n' → line 1, etc.
int sourceLineAt(const QByteArray &utf8, int byteOffset)
{
    const int end = qBound(0, byteOffset, int(utf8.size()));
    int lines = 0;
    for (int i = 0; i < end; ++i) {
        if (utf8[i] == '\n') ++lines;
    }
    return lines;
}
} // namespace


SceneCoordinator::SceneCoordinator(SelectionScene *scene, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
    , m_parser(new TreeSitterParser)
{
    m_reparseTimer = new QTimer(this);
    m_reparseTimer->setSingleShot(true);
    m_reparseTimer->setInterval(150);
    connect(m_reparseTimer, &QTimer::timeout, this, &SceneCoordinator::reparse);
}

SceneCoordinator::~SceneCoordinator()
{
    delete m_parser;
}

MarkdownTextItem *SceneCoordinator::createTextItem(const QString &text)
{
    auto *item = new MarkdownTextItem;
    item->setTextWidth(m_itemWidth);
    if (m_font.pointSize() > 0)
        item->document()->setDefaultFont(m_font);

    auto *highlighter = new MarkdownHighlighter(item->document());
    // Apply the coordinator's current theme to the new highlighter +
    // text-object handlers so freshly-created items pick up host-supplied
    // colors on first paint instead of flashing the light defaults.
    highlighter->setTheme(m_theme);
    item->setTheme(m_theme);

    // Set span map and decorated ranges BEFORE setPlainText. When
    // setPlainText triggers Qt's automatic highlightBlock calls,
    // both the span map and decorated ranges must already be available
    // for correct syntax coloring on the first render.
    if (m_parser->parse(text))
        highlighter->setSpanMap(m_parser->buildSpanMap());

    // Pre-detect decorated ranges from raw text so the highlighter
    // has them during the initial highlight pass.
    item->setPlainText(text);
    highlighter->setDecoratedRanges(item->decoratedRanges());
    highlighter->rehighlight();

    // Replace inline-math source with rendered glyphs.
    // The spans are now set, so the substitution can find them.
    item->refreshInlineSubstitutions();

    // Connect incremental span offset adjustment. Fires on every
    // document change BEFORE Qt's auto-rehighlight, keeping the
    // span map approximately correct between full reparses.
    connect(item->document(), &QTextDocument::contentsChange,
            highlighter, &MarkdownHighlighter::adjustSpanOffsets);

    m_scene->addItem(item);
    m_items.append(item);

    // Phase C3: disable Qt's built-in per-block undo history.
    // The canonical QUndoStack (MarkoffDocument) is the sole authoritative
    // undo source; allowing QTextDocument to accumulate its own history
    // would cause double-undo and state divergence.
    item->document()->setUndoRedoEnabled(false);

    // Phase C3 Task 15 — outbound delta: connect each item's QTextDocument
    // contentsChange to a lambda that translates (localPos, removed, added) →
    // canonical offset via the item map and pushes a MarkdownDelta.
    //
    // Capture via QPointer<MarkdownTextItem> (QObject subtype) so the lambda
    // stays safe across scene rebuilds where the item is replaced. At dispatch
    // time we look up the item's current index in m_itemMap rather than
    // capturing a stale index.
    {
        QPointer<MarkdownTextItem> itemRef(item);
        connect(item->document(), &QTextDocument::contentsChange,
                this, [this, itemRef](int pos, int removed, int added) {
            if (!itemRef) return;
            // Find current index of itemRef in m_itemMap at dispatch time.
            int idx = -1;
            for (int i = 0; i < m_itemMap.size(); ++i) {
                if (m_itemMap[i].item == itemRef.data()) { idx = i; break; }
            }
            if (idx < 0) return;
            this->onLocalItemContentsChange(idx, pos, removed, added);
        });
    }

    connect(item, &MarkdownTextItem::textChanged,
            this, &SceneCoordinator::onItemTextChanged);
    connect(item, &MarkdownTextItem::cursorAtBoundary,
            this, [this, item](Qt::Edge edge) {
        handleBoundary(item, edge);
    });

    return item;
}

void SceneCoordinator::captureFullDocumentParse(const QString &markdown)
{
    // `MarkdownSplitter::split()` already parsed `markdown` through
    // `m_parser`, so the parser is in full-document state right after
    // the call. Capture the headings and raw UTF-8 here before any
    // per-item parse replaces the parser's tree.
    m_rawHeadings = m_parser->buildDocumentQueries().headings;
    m_rawUtf8 = markdown.toUtf8();
}

QList<TableConverter::TableRegion>
SceneCoordinator::detectTableRegions(const QString &markdown) const
{
    QList<TableConverter::TableRegion> regions;
    // m_parser already parsed during the split — use findBlockBoundaries
    auto boundaries = m_parser->findBlockBoundaries();
    for (const auto &b : boundaries) {
        if (b.type != TreeSitterParser::BlockBoundary::Table)
            continue;

        // Tree-sitter's table node boundaries may not include the leading
        // pipe character or trailing content. Expand to full line boundaries
        // so the converter replaces ALL pipe text, not just the cell content.
        int start = b.startChar;
        while (start > 0 && markdown[start - 1] != QLatin1Char('\n'))
            --start;
        int end = b.endChar;
        while (end < markdown.length() && markdown[end] != QLatin1Char('\n'))
            ++end;
        // Do NOT include trailing newline — it serves as a separator
        // between the table and whatever follows. Including it causes
        // the converter to eat into adjacent content (especially a
        // second table that starts on the next non-blank line).

        QString tableText = markdown.mid(start, end - start);
        if (tableText.endsWith(QLatin1Char('\n')))
            tableText.chop(1);

        QStringList lines = tableText.split(QLatin1Char('\n'));
        if (lines.size() < 2)
            continue;

        TableConverter::TableRegion region;
        region.startPos = start;
        region.endPos = end;
        region.headers = TableHandler::parseRow(lines[0]);
        region.cols = region.headers.size();
        if (region.cols == 0) continue;

        // Line 1 is separator — parse alignments
        region.alignments = TableSerializer::parseAlignments(lines[1]);

        // Lines 2+ are data rows
        for (int i = 2; i < lines.size(); ++i) {
            QStringList row = TableHandler::parseRow(lines[i]);
            while (row.size() < region.cols) row.append(QString());
            while (row.size() > region.cols) row.removeLast();
            region.dataRows.append(row);
        }
        region.rows = 1 + region.dataRows.size();
        regions.append(region);
    }
    return regions;
}

void SceneCoordinator::loadMarkdown(const QString &markdown)
{
    clearItems();  // also clears m_tableConverters and m_itemMap
    auto segments = MarkdownSplitter::split(markdown, *m_parser);
    captureFullDocumentParse(markdown);

    // Build the canonical offset map alongside item creation.
    // `toMarkdown()` reconstructs the buffer as:
    //   items[0].toMarkdown() + '\n' + items[1].toMarkdown() + '\n' + ...
    // The inter-item '\n' separator occupies one char in the canonical buffer.
    // To keep the map contiguous (each item's canonicalEnd == next item's
    // canonicalStart), each non-last item absorbs the trailing separator into
    // its range. The last item's canonicalEnd == toMarkdown().length().
    // We compute canonicalStart correctly on the fly; canonicalEnd is fixed up
    // in a second pass after all items are created.
    int runningOffset = 0;

    for (int segIdx = 0; segIdx < segments.size(); ++segIdx) {
        const auto &seg = segments[segIdx];
        if (segIdx > 0)
            runningOffset += 1; // the '\n' join separator from toMarkdown()

        const int itemStart = runningOffset;
        const int itemEnd   = runningOffset + seg.text.length();

        if (seg.type == MarkdownSegment::Text) {
            auto *item = createTextItem(seg.text);

            // Detect and convert any pipe tables within this text item.
            // The document may contain U+FFFC inline substitutions (math,
            // checkboxes) from createTextItem() which shift positions.
            // Strip them first so the converter's offsets (from the raw
            // segment text) match the document's character positions.
            if (m_parser->parse(seg.text)) {
                auto regions = detectTableRegions(seg.text);
                // (debug removed)
                if (!regions.isEmpty()) {
                    // Block signals to prevent adjustSpanOffsets() from
                    // firing during the strip/convert/rebuild cycle.
                    // Without this, each document mutation triggers
                    // contentsChange → adjustSpanOffsets, AND
                    // applyInlineSubstitutions also adjusts spans
                    // manually — causing double adjustment.
                    QTextDocument *doc = item->document();
                    const bool blocked = doc->blockSignals(true);

                    item->stripInlineSubstitutions();

                    TableConverter &converter = m_tableConverters[item];
                    converter.convert(doc, regions);

                    // Rebuild span map in document coordinates, then
                    // re-apply inline substitutions.
                    // (debug removed)
                    const QString hlSrc = item->buildHighlightingSource();
                    if (m_parser->parse(hlSrc)) {
                        auto *hl = qobject_cast<MarkdownHighlighter *>(
                            doc->findChild<QSyntaxHighlighter *>());
                        if (hl) {
                            hl->setSpanMap(m_parser->buildSpanMap());
                        }
                    }
                    item->refreshInlineSubstitutions();

                    doc->blockSignals(blocked);
                    // Force a full rehighlight now that signals are
                    // unblocked and spans are in the correct form.
                    auto *hl = qobject_cast<MarkdownHighlighter *>(
                        doc->findChild<QSyntaxHighlighter *>());
                    if (hl) hl->rehighlight();
                }
            }

            m_itemMap.append({itemStart, itemEnd, item});
        } else if (seg.type == MarkdownSegment::Image) {
            auto *item = new ImageBlockItem(seg.text, m_itemWidth, m_resourceProvider);
            m_scene->addItem(item);
            m_items.append(item);
            m_itemMap.append({itemStart, itemEnd, item});
        }
        // No more TableBlockItem creation — tables now live inside text items

        runningOffset = itemEnd;
    }

    // Second pass: make the map contiguous. Each non-last item absorbs the
    // trailing '\n' separator into its canonicalEnd so that
    // m_itemMap[i].canonicalEnd == m_itemMap[i+1].canonicalStart.
    for (int i = 0; i + 1 < m_itemMap.size(); ++i)
        m_itemMap[i].canonicalEnd = m_itemMap[i + 1].canonicalStart;

    repositionItems();
    m_scene->setSelectableItems(m_items);
    m_headingMapDirty = true;
    emit reparsed();
}

int SceneCoordinator::sourceLineCount(const MarkdownTextItem *item)
{
    QTextDocument *doc = item->document();
    int lines = 0;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QString blockText = block.text();
        if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
            lines += 1 + blockText.count(QLatin1Char('\n'));
        } else {
            int blockNewlines = 0;
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                if (!frag.isValid()) continue;
                const QString raw = frag.charFormat()
                    .property(MathTextObject::RawProperty).toString();
                const QString t = frag.text();
                if (!raw.isEmpty() && t.size() == 1
                    && t.at(0) == QChar::ObjectReplacementCharacter) {
                    blockNewlines += raw.count(QLatin1Char('\n'));
                } else {
                    for (QChar c : t)
                        if (c == QLatin1Char('\n')) ++blockNewlines;
                }
            }
            lines += 1 + blockNewlines;
        }
    }
    return lines;
}

SceneCoordinator::GlobalPosition
SceneCoordinator::globalPositionOf(const MarkdownTextItem *item,
                                    int localBlockNumber,
                                    int columnInBlock) const
{
    int line = 1;
    for (int i = 0; i < m_items.size(); ++i) {
        // No inter-item transition: sourceLineCount (for text items) and
        // `1 + block.count('\n')` (for block items) already advance line by
        // the item's full source-line span, which equals the distance from
        // this item's first line to the next item's first line.

        if (m_items[i]->isTextItem()) {
            auto *mti = static_cast<MarkdownTextItem *>(m_items[i]);
            if (mti == item) {
                QTextDocument *doc = mti->document();
                for (QTextBlock block = doc->begin();
                     block.isValid() && block.blockNumber() < localBlockNumber;
                     block = block.next()) {
                    const QString blockText = block.text();
                    if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
                        line += 1 + blockText.count(QLatin1Char('\n'));
                    } else {
                        int blockNewlines = 0;
                        for (auto it = block.begin(); !it.atEnd(); ++it) {
                            const QTextFragment frag = it.fragment();
                            if (!frag.isValid()) continue;
                            const QString raw = frag.charFormat()
                                .property(MathTextObject::RawProperty).toString();
                            const QString t = frag.text();
                            if (!raw.isEmpty() && t.size() == 1
                                && t.at(0) == QChar::ObjectReplacementCharacter) {
                                blockNewlines += raw.count(QLatin1Char('\n'));
                            } else {
                                for (QChar c : t)
                                    if (c == QLatin1Char('\n')) ++blockNewlines;
                            }
                        }
                        line += 1 + blockNewlines;
                    }
                }
                return {line, columnInBlock + 1};
            }
            line += sourceLineCount(mti);
        } else {
            line += 1 + m_items[i]->toMarkdown().count(QLatin1Char('\n'));
        }
    }
    return {line, columnInBlock + 1};
}

SceneCoordinator::ItemPosition
SceneCoordinator::itemAtGlobalLine(int globalLine) const
{
    int line = 1;
    for (int i = 0; i < m_items.size(); ++i) {
        // See globalPositionOf: no inter-item transition needed.

        if (m_items[i]->isTextItem()) {
            auto *mti = static_cast<MarkdownTextItem *>(m_items[i]);
            int itemLines = sourceLineCount(mti);
            if (globalLine < line + itemLines) {
                int remaining = globalLine - line;
                QTextDocument *doc = mti->document();
                for (QTextBlock block = doc->begin();
                     block.isValid(); block = block.next()) {
                    if (remaining <= 0)
                        return {mti, block.blockNumber()};

                    const QString blockText = block.text();
                    int blockLines;
                    if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
                        blockLines = 1 + blockText.count(QLatin1Char('\n'));
                    } else {
                        int blockNewlines = 0;
                        for (auto it = block.begin(); !it.atEnd(); ++it) {
                            const QTextFragment frag = it.fragment();
                            if (!frag.isValid()) continue;
                            const QString raw = frag.charFormat()
                                .property(MathTextObject::RawProperty).toString();
                            const QString t = frag.text();
                            if (!raw.isEmpty() && t.size() == 1
                                && t.at(0) == QChar::ObjectReplacementCharacter) {
                                blockNewlines += raw.count(QLatin1Char('\n'));
                            } else {
                                for (QChar c : t)
                                    if (c == QLatin1Char('\n')) ++blockNewlines;
                            }
                        }
                        blockLines = 1 + blockNewlines;
                    }
                    remaining -= blockLines;
                }
                QTextBlock last = doc->lastBlock();
                return {mti, last.isValid() ? last.blockNumber() : 0};
            }
            line += itemLines;
        } else {
            int itemLines = 1 + m_items[i]->toMarkdown().count(QLatin1Char('\n'));
            if (globalLine < line + itemLines) {
                return {nullptr, 0};
            }
            line += itemLines;
        }
    }
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i]->isTextItem()) {
            auto *mti = static_cast<MarkdownTextItem *>(m_items[i]);
            QTextBlock last = mti->document()->lastBlock();
            return {mti, last.isValid() ? last.blockNumber() : 0};
        }
    }
    return {nullptr, 0};
}

void SceneCoordinator::removeBlockItem(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    if (m_items[index]->isTextItem()) return;

    m_scene->removeItem(m_items[index]->asGraphicsItem());
    delete m_items[index]->asGraphicsItem();
    m_items.removeAt(index);
    m_headingMapDirty = true;
}

QString SceneCoordinator::toMarkdown() const
{
    // Invariant: MarkdownSplitter guarantees the source is reproduced
    // byte-for-byte by joining each segment's text with a single '\n'
    // between segments. Every inter-item blank line lives inside some
    // text item's QTextDocument as empty QTextBlocks.
    QString result;
    for (int i = 0; i < m_items.size(); ++i) {
        if (i > 0)
            result += QLatin1Char('\n');
        result += m_items[i]->toMarkdown();
    }
    return result;
}

int SceneCoordinator::findItemIndexForOffset(int offset) const
{
    // Items are sorted by canonicalStart (linear splitter traversal guarantees
    // monotone order). Binary search for the item whose half-open range
    // [canonicalStart, canonicalEnd) contains offset.
    int lo = 0, hi = m_itemMap.size() - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const auto &e = m_itemMap[mid];
        if (offset < e.canonicalStart)
            hi = mid - 1;
        else if (offset >= e.canonicalEnd)
            lo = mid + 1;
        else
            return mid;
    }
    // End-of-buffer sentinel: offset == back().canonicalEnd maps to last item.
    if (!m_itemMap.isEmpty() && offset == m_itemMap.back().canonicalEnd)
        return m_itemMap.size() - 1;
    return -1;
}

void SceneCoordinator::shiftItemsAfter(int itemIndex, int delta)
{
    // Shift all items STRICTLY after itemIndex (not including itemIndex itself).
    for (int i = itemIndex + 1; i < m_itemMap.size(); ++i) {
        m_itemMap[i].canonicalStart += delta;
        m_itemMap[i].canonicalEnd   += delta;
    }
}

void SceneCoordinator::setItemWidth(qreal width)
{
    if (qFuzzyCompare(m_itemWidth, width))
        return;
    m_itemWidth = width;

    for (auto *item : m_items) {
        if (item->isTextItem()) {
            auto *textItem = static_cast<MarkdownTextItem *>(item);
            textItem->setTextWidth(width);
        } else if (auto *image = dynamic_cast<ImageBlockItem *>(item->asGraphicsItem())) {
            image->setMaxWidth(width);
        }
        // StubBlockItems have fixed width — they'll be replaced by real items later
    }
    repositionItems();
}

void SceneCoordinator::setResourceProvider(ResourceProvider *provider)
{
    m_resourceProvider = provider;
}

void SceneCoordinator::setTheme(const Theme &theme)
{
    m_theme = theme;
    for (auto *item : m_items) {
        if (item->isTextItem()) {
            auto *textItem = static_cast<MarkdownTextItem *>(item);
            textItem->setTheme(theme);
        } else if (auto *block = dynamic_cast<BlockItem *>(item->asGraphicsItem())) {
            block->setTheme(theme);
        }
    }
}

void SceneCoordinator::setFont(const QFont &font)
{
    m_font = font;
    for (auto *item : m_items) {
        if (item->isTextItem()) {
            auto *textItem = static_cast<MarkdownTextItem *>(item);
            textItem->document()->setDefaultFont(font);
        }
    }
    repositionItems();
}

bool SceneCoordinator::moveFocusTo(MarkdownTextItem *from, Qt::Edge edge)
{
    int idx = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i] == from) { idx = i; break; }
    }
    if (idx < 0) return false;

    // Find the next text item in the given direction
    int delta = (edge == Qt::TopEdge) ? -1 : 1;
    for (int i = idx + delta; i >= 0 && i < m_items.size(); i += delta) {
        if (m_items[i]->isTextItem()) {
            auto *target = static_cast<MarkdownTextItem *>(m_items[i]);
            target->setFocus();
            // Place cursor at appropriate end
            QTextCursor cursor(target->document());
            if (edge == Qt::TopEdge)
                cursor.movePosition(QTextCursor::End);
            else
                cursor.movePosition(QTextCursor::Start);
            target->textControl()->setTextCursor(cursor);
            return true;
        }
    }
    return false;
}

void SceneCoordinator::handleBoundary(MarkdownTextItem *from, Qt::Edge edge)
{
    bool shiftHeld = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    auto *mgr = m_scene->selectionManager();

    if (!shiftHeld) {
        if (mgr->mode() == SelectionMode::CrossBoundary) {
            mgr->clearSelection();
            m_keyboardCurrentIdx = -1;
            m_keyboardAnchorIdx = -1;
        }
        moveFocusTo(from, edge);
        return;
    }

    int dir = (edge == Qt::BottomEdge) ? 1 : -1;
    int fromIdx = m_items.indexOf(static_cast<SelectableItem *>(from));
    if (fromIdx < 0) return;

    // First time entering cross-boundary: record anchor
    if (mgr->mode() != SelectionMode::CrossBoundary) {
        QTextCursor cursor = from->textControl()->textCursor();
        m_keyboardAnchorPos = cursor.anchor();
        m_keyboardAnchorIdx = fromIdx;
        m_keyboardCurrentIdx = fromIdx;

        // Finalize anchor item's selection to its edge
        int edgePos = (edge == Qt::BottomEdge)
            ? from->documentLength() : 0;
        from->setSelection(m_keyboardAnchorPos, edgePos);
        mgr->beginOrExtendKeyboardSelection(from, m_keyboardAnchorPos, from, edgePos);
    }

    // Determine if extending (away from anchor) or contracting (toward anchor)
    bool extending = (dir > 0 && m_keyboardCurrentIdx >= m_keyboardAnchorIdx)
                  || (dir < 0 && m_keyboardCurrentIdx <= m_keyboardAnchorIdx);

    int nextIdx = m_keyboardCurrentIdx + dir;
    if (nextIdx < 0 || nextIdx >= m_items.size())
        return;

    if (extending) {
        // Advancing away from anchor — select the next item
        auto *next = m_items[nextIdx];
        m_keyboardCurrentIdx = nextIdx;

        if (next->isTextItem()) {
            // Transfer focus, place caret at entry edge
            auto *textItem = static_cast<MarkdownTextItem *>(next);
            textItem->setFocus();
            QTextCursor cursor(textItem->document());
            if (edge == Qt::BottomEdge)
                cursor.movePosition(QTextCursor::Start);
            else
                cursor.movePosition(QTextCursor::End);
            textItem->textControl()->setTextCursor(cursor);
        } else {
            // Block item — fully select it, focus stays on current text item
            next->setFullySelected(true);
        }

        mgr->beginOrExtendKeyboardSelection(
            mgr->anchorItem(), -1, m_items[nextIdx],
            next->isTextItem() ? 0 : -1);

    } else {
        // Contracting back toward anchor — deselect the current item
        auto *departing = m_items[m_keyboardCurrentIdx];
        if (departing->isTextItem())
            departing->clearSelection();
        else
            departing->setFullySelected(false);

        m_keyboardCurrentIdx = nextIdx;
        auto *arriving = m_items[m_keyboardCurrentIdx];

        if (m_keyboardCurrentIdx == m_keyboardAnchorIdx) {
            // Returned to anchor item — restore within-item selection
            auto *anchorText = static_cast<MarkdownTextItem *>(arriving);
            anchorText->setFocus();
            QTextCursor cursor(anchorText->document());
            cursor.setPosition(m_keyboardAnchorPos);
            // Position at the edge we arrived from
            int edgePos = (edge == Qt::BottomEdge)
                ? 0 : anchorText->documentLength();
            cursor.setPosition(edgePos, QTextCursor::KeepAnchor);
            anchorText->textControl()->setTextCursor(cursor);

            // Exit cross-boundary mode — back to within-item
            mgr->clearSelection();
            // Re-apply the within-item selection we just set
            anchorText->setSelection(m_keyboardAnchorPos, edgePos);
            m_keyboardCurrentIdx = -1;
            m_keyboardAnchorIdx = -1;

        } else if (arriving->isTextItem()) {
            // Arrived at a non-anchor text item — transfer focus
            auto *textItem = static_cast<MarkdownTextItem *>(arriving);
            textItem->setFocus();
            // Place caret at the edge we arrived from, with full selection
            QTextCursor cursor(textItem->document());
            if (edge == Qt::BottomEdge) {
                // Contracting upward, arriving from below
                cursor.movePosition(QTextCursor::Start);
                cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            } else {
                // Contracting downward, arriving from above
                cursor.movePosition(QTextCursor::End);
                cursor.movePosition(QTextCursor::Start, QTextCursor::KeepAnchor);
            }
            textItem->textControl()->setTextCursor(cursor);
            mgr->beginOrExtendKeyboardSelection(
                mgr->anchorItem(), -1, arriving, cursor.position());

        } else {
            // Arrived at a block — it stays selected (we just deselected the one beyond it)
            // Focus stays on the text item that still has focus
        }
    }
}

void SceneCoordinator::clearItems()
{
    for (auto *item : m_items) {
        m_scene->removeItem(item->asGraphicsItem());
        delete item->asGraphicsItem();
    }
    m_items.clear();
    m_tableConverters.clear();
    m_itemMap.clear();
}

void SceneCoordinator::repositionItems()
{
    qreal y = m_topMargin;
    for (auto *item : m_items) {
        QGraphicsItem *gi = item->asGraphicsItem();
        gi->setPos(m_leftMargin, y);
        if (gi->isVisible())
            y += gi->boundingRect().height() + m_spacing;
    }
    m_scene->setSceneRect(0, 0, m_leftMargin + m_itemWidth + m_leftMargin, y + m_topMargin);
}

void SceneCoordinator::onItemTextChanged()
{
    if (m_inReparse)
        return;
    m_reparseTimer->start(); // restart 150ms countdown
    emit textChanged();
}

void SceneCoordinator::reparse()
{
    m_inReparse = true;

    // Serialize current state
    QString markdown = toMarkdown();

    // Check if block boundaries changed
    auto newSegments = MarkdownSplitter::split(markdown, *m_parser);
    captureFullDocumentParse(markdown);

    // Compare segment count and types to current items
    bool structureChanged = false;
    if (newSegments.size() != m_items.size()) {
        structureChanged = true;
    } else {
        for (int i = 0; i < newSegments.size(); ++i) {
            bool wasText = m_items[i]->isTextItem();
            bool isText = (newSegments[i].type == MarkdownSegment::Text);
            if (wasText != isText) {
                structureChanged = true;
                break;
            }
        }
    }

    if (structureChanged) {
        clearItems();  // also clears m_tableConverters and m_itemMap

        int runningOffset = 0;
        for (int segIdx = 0; segIdx < newSegments.size(); ++segIdx) {
            const auto &seg = newSegments[segIdx];
            if (segIdx > 0)
                runningOffset += 1; // '\n' join separator from toMarkdown()

            const int itemStart = runningOffset;
            const int itemEnd   = runningOffset + seg.text.length();

            if (seg.type == MarkdownSegment::Text) {
                auto *item = createTextItem(seg.text);

                // Detect and convert pipe tables (same logic as loadMarkdown)
                if (m_parser->parse(seg.text)) {
                    auto regions = detectTableRegions(seg.text);
                    if (!regions.isEmpty()) {
                        QTextDocument *doc = item->document();
                        const bool blocked = doc->blockSignals(true);

                        item->stripInlineSubstitutions();

                        TableConverter &converter = m_tableConverters[item];
                        converter.convert(doc, regions);

                        const QString hlSrc = item->buildHighlightingSource();
                        if (m_parser->parse(hlSrc)) {
                            auto *hl = qobject_cast<MarkdownHighlighter *>(
                                doc->findChild<QSyntaxHighlighter *>());
                            if (hl)
                                hl->setSpanMap(m_parser->buildSpanMap());
                        }
                        item->refreshInlineSubstitutions();

                        doc->blockSignals(blocked);
                        auto *hl = qobject_cast<MarkdownHighlighter *>(
                            doc->findChild<QSyntaxHighlighter *>());
                        if (hl) hl->rehighlight();
                    }
                }

                m_itemMap.append({itemStart, itemEnd, item});
            } else if (seg.type == MarkdownSegment::Image) {
                auto *item = new ImageBlockItem(seg.text, m_itemWidth, m_resourceProvider);
                m_scene->addItem(item);
                m_items.append(item);
                m_itemMap.append({itemStart, itemEnd, item});
            }
            // No more TableBlockItem creation — tables live inside text items

            runningOffset = itemEnd;
        }

        // Make map contiguous: each non-last item absorbs the trailing '\n'
        // separator so m_itemMap[i].canonicalEnd == m_itemMap[i+1].canonicalStart.
        for (int i = 0; i + 1 < m_itemMap.size(); ++i)
            m_itemMap[i].canonicalEnd = m_itemMap[i + 1].canonicalStart;

        repositionItems();
        m_scene->setSelectableItems(m_items);
    } else {
        // Structure unchanged — update span maps using the shared parser.
        // Don't call rehighlight() — the adjustSpanOffsets connection keeps
        // spans approximately correct, and setSpanMap + targeted block
        // rehighlight on the next cursor-driven repaint is sufficient.
        //
        // Math substitution: each text item may currently contain U+FFFC
        // glyphs in place of $...$ regions. We strip them before applying
        // the new span map (so document offsets line up with span offsets),
        // then re-substitute afterwards.
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i]->isTextItem()) {
                auto *textItem = static_cast<MarkdownTextItem *>(m_items[i]);

                // Block document signals for the entire update cycle.
                // This prevents intermediate rehighlights between strip
                // (source form) and apply (substituted form) from leaving
                // formatting at stale character positions.
                QTextDocument *doc = textItem->document();
                const bool blocked = doc->blockSignals(true);

                textItem->stripInlineSubstitutions();

                // Parse the document-coordinate-aligned highlighting
                // source. Table cell blocks appear as newlines (the
                // highlighter's frame guard skips them anyway), and
                // all non-table text is at its true document position.
                // Spans from tree-sitter are already in document
                // coordinates — no offset remapping needed.
                const QString hlSrc = textItem->buildHighlightingSource();
                if (m_parser->parse(hlSrc)) {
                    auto *highlighter = qobject_cast<MarkdownHighlighter *>(
                        doc->findChild<QSyntaxHighlighter *>());

                    // Keep table records in sync with the document.
                    TableConverter &converter = m_tableConverters[textItem];
                    converter.reconcile(doc, {});

                    if (highlighter) {
                        highlighter->setSpanMap(m_parser->buildSpanMap());
                        textItem->invalidateSourcePositionSpans();
                    }
                }

                textItem->refreshBlockFormatting();
                textItem->refreshInlineSubstitutions();

                doc->blockSignals(blocked);
            }
        }
        repositionItems();
    }

    // Clear `m_inReparse` synchronously before emitting. A prior
    // version deferred the clear via `QTimer::singleShot(0, ...)`
    // which caused any synchronous `reparsed` handler that edited
    // text to have its follow-up reparse suppressed:
    // `onItemTextChanged` saw the still-true guard and bailed out
    // without restarting the debounce timer. Edits from reparsed
    // handlers now correctly restart the timer.
    m_inReparse = false;
    emit reparsed();
}

void SceneCoordinator::setFoldingModel(FoldingModel *model)
{
    if (m_foldingModel)
        disconnect(m_foldingModel, nullptr, this, nullptr);
    m_foldingModel = model;
    m_headingMapDirty = true;
    if (m_foldingModel) {
        connect(m_foldingModel, &FoldingModel::foldStateChanged,
                this, [this]() {
                    m_headingMapDirty = true;
                    applyFoldVisibility();
                });
    }
}

void SceneCoordinator::ensureHeadingMap() const
{
    if (!m_headingMapDirty) return;
    m_blockToHeadingIdx.clear();
    m_headingMapDirty = false;
    if (!m_foldingModel) return;
    const auto &hs = m_foldingModel->headings();
    if (hs.isEmpty()) return;


    // Use sourceOffsets captured during the last full-document parse
    // (see `captureFullDocumentParse()`, populated by
    // `loadMarkdown()` / `reparse()` right after `MarkdownSplitter::split()`).
    // `Document::fromMarkdown` — which populates `FoldingModel`'s
    // headings — strips frontmatter and footnote definitions before
    // parsing, so its offsets don't align with `toMarkdown()`'s byte
    // space. The cached raw-document headings match `toMarkdown()`'s
    // byte space directly, and we match them against
    // `FoldingModel::headings()` by document-order index.
    const QByteArray &utf8 = m_rawUtf8;
    const QList<HeadingInfo> &rawHeadings = m_rawHeadings;
    if (rawHeadings.size() != hs.size()) return;

    QHash<int, int> lineToHeadingIdx;
    lineToHeadingIdx.reserve(rawHeadings.size());
    for (int i = 0; i < rawHeadings.size(); ++i) {
        const int line = sourceLineAt(utf8, rawHeadings[i].sourceOffset);
        lineToHeadingIdx.insert(line, i);
    }

    // Walk items in order, tracking the running source-line offset.
    //
    // Within a text item we iterate the ROOT frame (not `doc->begin()`
    // / `block.next()`) so that `QTextTable` child frames are treated
    // as single atomic elements. That matches `allMarkdown()`'s
    // serialization: a table emits via `TableSerializer::serialize()`
    // rather than one line per cell-block. Iterating per-block would
    // over-count source lines at every table, misaligning the heading
    // map (and the gutter triangles) for every heading past the first
    // table in a document.
    //
    // A single QTextBlock can still expand to MULTIPLE source lines
    // when it holds an ObjectReplacementCharacter whose RawProperty is
    // a multi-line string (display math `$$\n...\n$$`), so we keep the
    // ORC-expansion branch.
    int srcLine = 0;
    for (int itemIdx = 0; itemIdx < m_items.size(); ++itemIdx) {
        if (itemIdx > 0)
            srcLine += 1;
        if (m_items[itemIdx]->isTextItem()) {
            auto *mti = static_cast<MarkdownTextItem *>(m_items[itemIdx]);
            int blockLine = srcLine;
            QTextFrame *root = mti->document()->rootFrame();
            bool firstElem = true;
            for (auto fit = root->begin(); fit != root->end(); ++fit) {
                if (!firstElem) blockLine += 1; // inter-element separator newline
                firstElem = false;

                if (auto *childFrame = fit.currentFrame()) {
                    if (auto *table = qobject_cast<QTextTable *>(childFrame)) {
                        // Tree-sitter sees `TableSerializer::serialize(table)`,
                        // not the raw cell blocks. A table produces
                        // (newline-count + 1) source lines; advance by the
                        // newline count since the "+1" is the separator added
                        // by the next iteration.
                        const QString serialized = TableSerializer::serialize(table);
                        blockLine += int(serialized.count(QLatin1Char('\n')));
                    }
                    continue;
                }

                QTextBlock block = fit.currentBlock();
                if (!block.isValid()) continue;

                auto it = lineToHeadingIdx.constFind(blockLine);
                if (it != lineToHeadingIdx.constEnd())
                    m_blockToHeadingIdx.insert({itemIdx, block.blockNumber()}, *it);

                // Compute this block's own newline count, expanding any
                // ORC fragments to their RawProperty source (same rule
                // allMarkdown() uses to reconstruct the markdown).
                int blockNewlines = 0;
                const QString blockText = block.text();
                if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
                    blockNewlines = int(blockText.count(QLatin1Char('\n')));
                } else {
                    for (auto fragIt = block.begin(); !fragIt.atEnd(); ++fragIt) {
                        const QTextFragment frag = fragIt.fragment();
                        if (!frag.isValid()) continue;
                        const QString raw = frag.charFormat()
                            .property(MathTextObject::RawProperty).toString();
                        const QString t = frag.text();
                        if (!raw.isEmpty() && t.size() == 1
                            && t.at(0) == QChar::ObjectReplacementCharacter) {
                            blockNewlines += int(raw.count(QLatin1Char('\n')));
                        } else {
                            for (QChar c : t)
                                if (c == QLatin1Char('\n')) ++blockNewlines;
                        }
                    }
                }
                blockLine += blockNewlines;
            }
            srcLine = blockLine;
        } else {
            srcLine += int(m_items[itemIdx]->toMarkdown().count(QLatin1Char('\n')));
        }
    }
}

int SceneCoordinator::headingAtBlock(int itemIdx, int blockNumber) const
{
    ensureHeadingMap();
    return m_blockToHeadingIdx.value({itemIdx, blockNumber}, -1);
}

int SceneCoordinator::itemIndexAt(qreal sceneY) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        QGraphicsItem *gi = m_items[i]->asGraphicsItem();
        if (!gi) continue;
        const QRectF r = gi->sceneBoundingRect();
        if (sceneY >= r.top() && sceneY <= r.bottom())
            return i;
    }
    return -1;
}

QStringList SceneCoordinator::enclosingHeadingPath(int itemIndex) const
{
    if (!m_foldingModel) return {};
    const auto &hs = m_foldingModel->headings();
    if (hs.isEmpty()) return {};

    // The most-recent heading at or before itemIndex (across all blocks).
    // Use the AST-derived map; ties broken by document order.
    int hIdx = -1;
    for (int i = 0; i <= itemIndex && i < m_items.size(); ++i) {
        if (!m_items[i]->isTextItem()) continue;
        auto *mti = static_cast<MarkdownTextItem *>(m_items[i]);
        QTextBlock block = mti->document()->begin();
        while (block.isValid()) {
            const int h = headingAtBlock(i, block.blockNumber());
            if (h >= 0) hIdx = h;
            block = block.next();
        }
    }
    return hIdx >= 0 ? hs[hIdx].path : QStringList{};
}

QStringList SceneCoordinator::enclosingHeadingPathAtBlock(int itemIndex, int blockNumber) const
{
    if (!m_foldingModel) return {};
    const auto &hs = m_foldingModel->headings();
    if (hs.isEmpty()) return {};

    // Use the AST-derived map. Within the target item, stop after blockNumber
    // so headings later in the same item don't supersede the match position.
    int hIdx = -1;
    for (int i = 0; i <= itemIndex && i < m_items.size(); ++i) {
        if (!m_items[i]->isTextItem()) continue;
        auto *mti = static_cast<MarkdownTextItem *>(m_items[i]);

        QTextBlock block = mti->document()->begin();
        while (block.isValid()) {
            if (i == itemIndex && block.blockNumber() > blockNumber) break;
            const int h = headingAtBlock(i, block.blockNumber());
            if (h >= 0) hIdx = h;
            block = block.next();
        }
    }

    return hIdx >= 0 ? hs[hIdx].path : QStringList{};
}

int SceneCoordinator::headingIndexForItem(int itemIndex) const
{
    if (!m_foldingModel) return -1;
    if (itemIndex < 0 || itemIndex >= m_items.size()) return -1;
    if (!m_items[itemIndex]->isTextItem()) return -1;
    auto *mti = static_cast<MarkdownTextItem *>(m_items[itemIndex]);
    // The item is a "heading item" iff its first block is a heading.
    return headingAtBlock(itemIndex, 0);
}

void SceneCoordinator::applyFoldVisibility()
{
    if (!m_foldingModel) return;
    const auto &hs = m_foldingModel->headings();

    // Walk all items. For text items, operate at QTextBlock granularity so
    // that individual paragraphs and headings within a single MarkdownTextItem
    // can be shown/hidden independently (MarkdownSplitter does not split at
    // heading boundaries — a whole section may live in one item).
    //
    // For non-text items (tables/images), operate at the item level.
    int hIdx = -1;  // index of current enclosing heading in hs[]

    for (int itemIdx = 0; itemIdx < m_items.size(); ++itemIdx) {
        if (!m_items[itemIdx]->isTextItem()) {
            // Non-text item: hide/show based on enclosing heading.
            const QStringList path = (hIdx >= 0) ? hs[hIdx].path : QStringList{};
            bool hidden = !path.isEmpty()
                && (m_foldingModel->isFolded(path)
                    || m_foldingModel->isHiddenByFold(path));
            m_items[itemIdx]->asGraphicsItem()->setVisible(!hidden);
            continue;
        }

        // Text item: walk its root frame so that QTextTable child frames
        // are handled atomically. Per-cell block iteration would leak the
        // table's borders / padding through on fold — setTableFolded()
        // zeros those out.
        auto *mti = static_cast<MarkdownTextItem *>(m_items[itemIdx]);
        QTextDocument *doc = mti->document();
        bool anyBlockVisible = false;

        QTextFrame *root = doc->rootFrame();
        for (auto fit = root->begin(); fit != root->end(); ++fit) {
            if (auto *childFrame = fit.currentFrame()) {
                if (auto *table = qobject_cast<QTextTable *>(childFrame)) {
                    // A table inherits the currently-enclosing heading's
                    // fold state; it is body content, not a heading.
                    bool hidden = false;
                    if (hIdx >= 0) {
                        const QStringList &path = hs[hIdx].path;
                        hidden = m_foldingModel->isFolded(path)
                              || m_foldingModel->isHiddenByFold(path);
                    }
                    mti->setTableFolded(table, hidden);
                    if (!hidden) anyBlockVisible = true;
                }
                continue;
            }

            QTextBlock block = fit.currentBlock();
            if (!block.isValid()) continue;

            const int h = headingAtBlock(itemIdx, block.blockNumber());
            const bool isHeading = (h >= 0);
            if (isHeading) hIdx = h;

            bool hidden = false;
            if (hIdx >= 0) {
                const QStringList &path = hs[hIdx].path;
                if (isHeading) {
                    // The heading line itself stays visible unless an ANCESTOR
                    // heading is folded (isHiddenByFold checks strict prefixes).
                    hidden = m_foldingModel->isHiddenByFold(path);
                } else {
                    // Body block: hidden if the enclosing heading is folded
                    // OR any ancestor heading is folded.
                    hidden = m_foldingModel->isFolded(path)
                          || m_foldingModel->isHiddenByFold(path);
                }
            }

            mti->setBlockFolded(block.blockNumber(), hidden);
            if (!hidden) anyBlockVisible = true;
        }

        // The item stays in the scene (we never remove it); its QGraphicsItem
        // visibility tracks whether it has ANY visible content.
        m_items[itemIdx]->asGraphicsItem()->setVisible(anyBlockVisible);
    }
    repositionItems();
}

int SceneCoordinator::headingIndexAtSceneY(qreal sceneY) const
{
    for (int itemIdx = 0; itemIdx < m_items.size(); ++itemIdx) {
        if (!m_items[itemIdx]->isTextItem()) continue;
        auto *mti = static_cast<MarkdownTextItem *>(m_items[itemIdx]);

        QGraphicsItem *gi = mti->asGraphicsItem();
        if (!gi) continue;
        const qreal itemSceneY = gi->scenePos().y();

        QTextBlock block = mti->document()->begin();
        while (block.isValid()) {
            const int h = headingAtBlock(itemIdx, block.blockNumber());
            if (h >= 0) {
                QTextLayout *layout = block.layout();
                qreal blockTop = itemSceneY;
                qreal blockHeight = 16.0;
                if (layout) {
                    blockTop = itemSceneY + layout->position().y();
                    if (layout->lineCount() > 0)
                        blockHeight = layout->boundingRect().height();
                }
                if (sceneY >= blockTop && sceneY < blockTop + blockHeight)
                    return h;
            }
            block = block.next();
        }
    }
    return -1;
}

qreal SceneCoordinator::headingSceneY(int headingIndex) const
{
    if (headingIndex < 0) return -1.0;

    for (int itemIdx = 0; itemIdx < m_items.size(); ++itemIdx) {
        if (!m_items[itemIdx]->isTextItem()) continue;
        auto *mti = static_cast<MarkdownTextItem *>(m_items[itemIdx]);

        QGraphicsItem *gi = mti->asGraphicsItem();
        if (!gi) continue;
        const qreal itemSceneY = gi->scenePos().y();

        QTextBlock block = mti->document()->begin();
        while (block.isValid()) {
            if (headingAtBlock(itemIdx, block.blockNumber()) == headingIndex) {
                QTextLayout *layout = block.layout();
                return layout ? itemSceneY + layout->position().y() : itemSceneY;
            }
            block = block.next();
        }
    }
    return -1.0;
}

// -------------------------------------------------------------------------
// Phase C3 Task 15 — outbound delta: setBoundDocument + onLocalItemContentsChange
// -------------------------------------------------------------------------

void SceneCoordinator::setBoundDocument(Markoff::MarkoffDocument *doc)
{
    m_boundDoc = doc;
}

void SceneCoordinator::onLocalItemContentsChange(int itemIndex, int localPos,
                                                  int charsRemoved, int charsAdded)
{
    // Guard: skip if we are currently applying an inbound canonical delta
    // (Task 16) or if no document is bound.
    if (m_applyingCanonicalDelta) return;
    if (!m_boundDoc) return;
    if (itemIndex < 0 || itemIndex >= m_itemMap.size()) return;

    const auto &entry = m_itemMap[itemIndex];
    const qsizetype canonicalOffset = entry.canonicalStart + qsizetype(localPos);

    // Extract the inserted text from the local document before pushing the
    // delta (the delta's first redo() will capture the removed text from the
    // canonical buffer, so we only need to provide the insertion here).
    QString insertedText;
    if (charsAdded > 0) {
        if (entry.item && entry.item->isTextItem()) {
            auto *mti = static_cast<MarkdownTextItem *>(entry.item);
            QTextDocument *td = mti->document();
            QTextCursor c(td);
            c.setPosition(localPos);
            c.setPosition(localPos + charsAdded, QTextCursor::KeepAnchor);
            insertedText = c.selectedText();
            // Qt uses U+2029 (ParagraphSeparator) for paragraph breaks inside
            // a QTextDocument; normalize to '\n' for the canonical buffer.
            insertedText.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        }
    }

    m_applyingCanonicalDelta = true;
    m_boundDoc->undoStack()->push(
        new Markoff::MarkdownDelta(m_boundDoc,
                                   canonicalOffset,
                                   qsizetype(charsRemoved),
                                   insertedText));
    m_applyingCanonicalDelta = false;
}

} // namespace Markoff
