// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/StructuralKeyHandler.h>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/StructuralOp.h>

#include <algorithm>

namespace Markoff {
namespace {
bool accepted(const VNext::ActionPublicationResult& result)
{
    return result.state == VNext::ActionPublicationState::committed
        || result.state == VNext::ActionPublicationState::committedHistoryPending;
}

StructuralResult split(MarkoffDocument& doc, BlockId block, BlockKind kind,
                       uint32_t offset, uint32_t nextOffset = 0,
                       const QList<AttrName>& attrNames = {},
                       const QList<AttrValue>& attrValues = {},
                       const QList<BlockAttrUpdate>& siblingUpdates = {})
{
    const auto blocks = doc.iterateBlocks();
    const auto at = std::find(blocks.begin(), blocks.end(), block);
    if (at == blocks.end()) return {};
    const auto text = doc.blockText(block);
    if (offset == 0 && at == blocks.begin()) {
        // Document-start continuation, no previous block to split. Flip
        // the split instead: this block sheds its WHOLE text into a new
        // block after it, keeping the run's start attrs on the retained
        // (now empty) first block, while the continuation attrs flow to
        // the new block exactly like the offset>0 case. The flip matters
        // because a whole-text split produces text operations inside the
        // action, so undoD2's compensation dispatches on the same
        // mixed-list/map/text shape every other split polarises through;
        // a bare applyStructural insert (idList + maps, no text) has no
        // compensator and would undo as a no-op.
        const auto result = doc.applyBlockSplit({block, 0,
            static_cast<uint32_t>(text.size()), {}}, kind, {},
            attrNames, attrValues, siblingUpdates);
        if (!accepted(result)) return {};
        // The caret lands at the start of the emptied retained block.
        return {true, block, 0};
    }
    if (offset == 0) {
        const BlockId previous = *(at - 1);
        const uint32_t end = static_cast<uint32_t>(doc.blockText(previous).size());
        const auto result = doc.applyBlockSplit({previous, end, 0, {}}, kind, {},
            attrNames, attrValues, siblingUpdates);
        if (!accepted(result)) return {};
        const auto after = doc.iterateBlocks();
        const auto previousIt = std::find(after.begin(), after.end(), previous);
        if (previousIt == after.end() || previousIt + 1 == after.end()) return {};
        return {true, *(previousIt + 1), nextOffset};
    }
    const auto result = doc.applyBlockSplit({block, offset,
        static_cast<uint32_t>(text.size()) - offset, {}}, kind, {},
        attrNames, attrValues, siblingUpdates);
    if (!accepted(result)) return {};
    const auto after = doc.iterateBlocks();
    const auto current = std::find(after.begin(), after.end(), block);
    if (current == after.end() || current + 1 == after.end()) return {};
    return {true, *(current + 1), nextOffset};
}

StructuralResult mergePrevious(MarkoffDocument& doc, BlockId block,
                               uint32_t caretOffset)
{
    const auto blocks = doc.iterateBlocks();
    const auto at = std::find(blocks.begin(), blocks.end(), block);
    if (at == blocks.begin() || at == blocks.end()) return {};
    const BlockId previous = *(at - 1);
    const uint32_t offset = static_cast<uint32_t>(doc.blockText(previous).size());
    if (!accepted(doc.applyBlockMerge({previous, offset, 0, doc.blockText(block)}, {block})))
        return {};
    return {true, previous, offset + caretOffset};
}

StructuralResult mergeNext(MarkoffDocument& doc, BlockId block, uint32_t offset)
{
    const auto blocks = doc.iterateBlocks();
    const auto at = std::find(blocks.begin(), blocks.end(), block);
    if (at == blocks.end() || at + 1 == blocks.end()) return {};
    const BlockId next = *(at + 1);
    if (!accepted(doc.applyBlockMerge({block,
            static_cast<uint32_t>(doc.blockText(block).size()), 0,
            doc.blockText(next)}, {next}))) return {};
    return {true, block, offset};
}
}

StructuralResult StructuralKeyHandler::handle(MarkoffDocument& doc, BlockId block,
                                               int key, int modifiers,
                                               uint32_t caretByteInBlock)
{
    if (block.isNull()) return {};
    if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; modifiers |= Qt::ShiftModifier; }
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    const auto kind = doc.blockKind(block);
    const auto text = doc.blockText(block);
    const uint32_t length = static_cast<uint32_t>(text.size());
    const uint32_t offset = std::min(caretByteInBlock, length);

    if (enter) {
        if ((modifiers & Qt::ShiftModifier) || kind == BlockKind::CodeBlock
            || (kind == BlockKind::Math && offset < length)) {
            return accepted(doc.applyBlockEdit({block, offset, 0, QByteArrayLiteral("\n")}))
                ? StructuralResult{true, block, offset + 1} : StructuralResult{};
        }
        if (kind == BlockKind::ListItem && text.isEmpty()) {
            const auto result = doc.changeBlockKind(block, BlockKind::Paragraph,
                {AttrNames::MarkerStyle}, {QString{}});
            return accepted(result) ? StructuralResult{true, block, 0} : StructuralResult{};
        }
        BlockKind nextKind = BlockKind::Paragraph;
        if (kind == BlockKind::ListItem || kind == BlockKind::BlockQuote)
            nextKind = kind;
        if (kind == BlockKind::Math && offset != length) return {};

        // Continuation semantics (what typing Enter in a list item or
        // quotation does in every word processor): the split-off block
        // stays in its run — same indent level, same marker style, same
        // quote depth/run — instead of falling back to the kind defaults
        // (a top-level plain "-" bullet) that a bare split used to give.
        // Deliberate exceptions:
        //   - ordered items keep the run sequential: the continuation
        //     takes the next number and every same-run item after the
        //     split point shifts in the same action; an Enter at position
        //     0 inserts an empty item ABOVE whose number continues from
        //     the adjacent previous item (or the run's start number), so
        //     the split block itself shifts too;
        //   - Checked is never copied. A task item's continuation starts
        //     UNCHECKED, and the fresh `false` write must exist so the
        //     checkbox stays clickable (toggleListItemChecked refuses to
        //     toggle an item without the attr) — the same invariant the
        //     parser maintains for every loaded task item.
        QList<AttrName> continuationNames;
        QList<AttrValue> continuationValues;
        QList<BlockAttrUpdate> siblingUpdates;
        if (nextKind == kind) {
            const auto attrs = doc.blockAttrs(block);
            QString markerStyle;
            int srcIndent = 0;
            int srcNumber = 0;
            if (const auto it = attrs.constFind(AttrNames::MarkerStyle);
                it != attrs.cend())
                if (const auto* style = std::get_if<QString>(&it.value()))
                    markerStyle = *style;
            if (const auto it = attrs.constFind(AttrNames::IndentLevel);
                it != attrs.cend())
                if (const auto* value = std::get_if<int>(&it.value()))
                    srcIndent = *value;
            if (const auto it = attrs.constFind(AttrNames::MarkerNumber);
                it != attrs.cend())
                if (const auto* value = std::get_if<int>(&it.value()))
                    srcNumber = *value;
            const auto numberIn = [](const QHash<AttrName, AttrValue>& candidate,
                                     int fallback = 0) {
                const auto it = candidate.constFind(AttrNames::MarkerNumber);
                if (it == candidate.cend()) return fallback;
                const auto* value = std::get_if<int>(&it.value());
                return value ? *value : fallback;
            };
            const auto indentIn = [&numberIn](const QHash<AttrName, AttrValue>& candidate) {
                const auto it = candidate.constFind(AttrNames::IndentLevel);
                if (it == candidate.cend()) return 0;
                const auto* value = std::get_if<int>(&it.value());
                return value ? *value : 0;
            };
            const auto orderedStyle = [](const QString& style) {
                return style == QStringLiteral("dot") || style == QStringLiteral("paren");
            };
            const bool isOrdered = orderedStyle(markerStyle);
            const bool task = markerStyle == QStringLiteral("task");

            // Ordered run: the continuation's own number, and where the
            // downstream renumber walk starts. The walk only runs for a
            // real run (a stored positive number); unseen numbers stay
            // untouched, mirroring today's semantic for marker-less
            // continuations.
            int continuationNumber = 0;
            int downstreamStartNumber = 0;
            qsizetype downstreamFrom = -1;
            const auto blocks = doc.iterateBlocks();
            const auto srcIt = std::find(blocks.begin(), blocks.end(), block);
            const qsizetype srcIndex =
                srcIt == blocks.end() ? -1 : std::distance(blocks.begin(), srcIt);
            if (isOrdered && srcNumber > 0 && srcIndex >= 0) {
                continuationNumber = srcNumber + 1;
                if (offset > 0) {
                    // Continuation lands after this block; the number walk
                    // starts one past the continuation's own number.
                    downstreamFrom = srcIndex + 1;
                    downstreamStartNumber = continuationNumber + 1;
                } else if (srcIndex > 0) {
                    // Enter at position 0 inserts the empty continuation
                    // BETWEEN the previous block and this one. Adjacent
                    // same-run previous: the continuation continues it and
                    // this block leads the downstream walk; otherwise the
                    // continuation takes this block's number as the run's
                    // start and this block still shifts.
                    const auto previous = doc.blockAttrs(blocks[size_t(srcIndex) - 1]);
                    const int previousNumber = numberIn(previous, 0);
                    if (doc.blockKind(blocks[size_t(srcIndex) - 1])
                            == BlockKind::ListItem
                        && indentIn(previous) == srcIndent
                        && previousNumber > 0) {
                        continuationNumber = previousNumber + 1;
                    }
                    downstreamFrom = srcIndex;
                    downstreamStartNumber = continuationNumber + 1;
                }
            }

            for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it) {
                if (it.key() == AttrNames::Checked) continue;
                if (it.key() == AttrNames::MarkerNumber && !isOrdered) continue;
                if (it.key() == AttrNames::MarkerNumber) {
                    continuationNames.push_back(it.key());
                    continuationValues.push_back(continuationNumber > 0
                        ? continuationNumber : 1);
                    continue;
                }
                continuationNames.push_back(it.key());
                continuationValues.push_back(it.value());
            }
            if (task) {
                continuationNames.push_back(AttrNames::Checked);
                continuationValues.push_back(false);
            }
            if (downstreamFrom >= 0 && downstreamStartNumber > 0) {
                // Same-run walk: contiguous list items at this indent and
                // marker style renumber sequentially; deeper indents (a
                // nested list's own run) are skipped over without a write;
                // a shallower item, a non-list block, or a different marker
                // style ends the run. Items whose stored number already
                // matches get no write at all.
                int nextNumber = downstreamStartNumber;
                for (qsizetype i = downstreamFrom; i < blocks.size(); ++i) {
                    if (doc.blockKind(blocks[i]) != BlockKind::ListItem) break;
                    const auto sibling = doc.blockAttrs(blocks[i]);
                    const int indent = indentIn(sibling);
                    if (indent > srcIndent) continue;  // nested list's own run
                    if (indent != srcIndent) break;
                    QString siblingStyle;
                    if (const auto it = sibling.constFind(AttrNames::MarkerStyle);
                        it != sibling.cend())
                        if (const auto* style = std::get_if<QString>(&it.value()))
                            siblingStyle = *style;
                    if (siblingStyle != markerStyle) break;
                    if (numberIn(sibling, 0) != nextNumber)
                        siblingUpdates.push_back(
                            {blocks[i], AttrNames::MarkerNumber, nextNumber});
                    ++nextNumber;
                }
            }
        }
        if (kind == BlockKind::Paragraph || kind == BlockKind::Heading
            || kind == BlockKind::ListItem || kind == BlockKind::BlockQuote
            || kind == BlockKind::Math || kind == BlockKind::HorizontalRule)
            return split(doc, block, nextKind, offset, 0,
                continuationNames, continuationValues, siblingUpdates);
        return {};
    }
    if (kind == BlockKind::ListItem && key == Qt::Key_Backspace && offset == 0) {
        int indent = 0;
        const auto attrs = doc.blockAttrs(block);
        if (const auto it = attrs.constFind(AttrNames::IndentLevel);
            it != attrs.cend())
            if (const int* value = std::get_if<int>(&it.value())) indent = *value;
        if (indent > 0) {
            const auto result = doc.changeBlockKind(block, BlockKind::ListItem,
                {AttrNames::IndentLevel}, {indent - 1});
            return accepted(result) ? StructuralResult{true, block, 0} : StructuralResult{};
        }
        const auto result = doc.changeBlockKind(block, BlockKind::Paragraph,
            {AttrNames::MarkerStyle}, {QString{}});
        return accepted(result) ? StructuralResult{true, block, 0} : StructuralResult{};
    }
    if (kind == BlockKind::ListItem && key == Qt::Key_Tab) {
        const bool outdent = (modifiers & Qt::ShiftModifier) != 0;
        const auto attrs = doc.blockAttrs(block);
        int indent = 0;
        if (const auto it = attrs.constFind(AttrNames::IndentLevel);
            it != attrs.cend())
            if (const int* value = std::get_if<int>(&it.value())) indent = *value;
        const int nextIndent = outdent ? std::max(0, indent - 1)
                                       : std::min(6, indent + 1);
        if (nextIndent == indent) return {true, block, offset};
        const auto blocks = doc.iterateBlocks();
        const auto at = std::find(blocks.begin(), blocks.end(), block);
        if (!outdent && at != blocks.end()) {
            bool parentFound = false;
            while (at != blocks.begin()) {
                const BlockId previous = *(at - 1);
                if (doc.blockKind(previous) != BlockKind::ListItem) break;
                const auto previousAttrs = doc.blockAttrs(previous);
                int previousIndent = 0;
                if (const auto it = previousAttrs.constFind(AttrNames::IndentLevel);
                    it != previousAttrs.cend())
                    if (const int* value = std::get_if<int>(&it.value()))
                        previousIndent = *value;
                if (previousIndent == indent) { parentFound = true; break; }
                if (previousIndent < indent) break;
                // Continue searching toward the start of this list run.
                // `at` is const, so use an explicit index below instead.
                break;
            }
            if (!parentFound) {
                int index = static_cast<int>(std::distance(blocks.begin(), at)) - 1;
                while (index >= 0) {
                    const BlockId previous = blocks[static_cast<size_t>(index)];
                    if (doc.blockKind(previous) != BlockKind::ListItem) break;
                    const auto previousAttrs = doc.blockAttrs(previous);
                    int previousIndent = 0;
                    if (const auto it = previousAttrs.constFind(AttrNames::IndentLevel);
                        it != previousAttrs.cend())
                        if (const int* value = std::get_if<int>(&it.value()))
                            previousIndent = *value;
                    if (previousIndent == indent) { parentFound = true; break; }
                    if (previousIndent < indent) break;
                    --index;
                }
            }
            if (!parentFound) return {true, block, offset};
        }
        const auto result = doc.changeBlockKind(block, BlockKind::ListItem,
            {AttrNames::IndentLevel}, {nextIndent});
        return accepted(result) ? StructuralResult{true, block, offset} : StructuralResult{};
    }
    if (key == Qt::Key_Backspace && offset == 0)
        return mergePrevious(doc, block, 0);
    if (key == Qt::Key_Delete && offset == length)
        return mergeNext(doc, block, offset);
    if (key == Qt::Key_Tab && kind == BlockKind::CodeBlock
        && !(modifiers & Qt::ShiftModifier)) {
        return accepted(doc.applyBlockEdit({block, offset, 0, QByteArrayLiteral("    ")}))
            ? StructuralResult{true, block, offset + 4} : StructuralResult{};
    }
    return {};
}

} // namespace Markoff
