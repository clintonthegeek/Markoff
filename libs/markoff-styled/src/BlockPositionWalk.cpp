// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockPositionWalk.h"

#include <QTextDocument>
#include <QTextFrame>

#include <markoff/core/MarkoffDocument.h>

namespace Markoff::Styled {

bool walkBlocks(const Markoff::MarkoffDocument *doc, QTextDocument *qdoc,
                const std::function<void(const WalkEntry &)> &visit) {
    if (!doc || !qdoc) return false;

    // The QTextDocument is populated by the binding's opaque-aware seed:
    // each model block maps to one-or-more top-level QTextDocument elements
    // (a paragraph is one QTextBlock; a code block is N QTextBlocks where
    // N = lines; a Table is a QTextTable child frame). We CANNOT derive
    // positions from flat pipe-source bytes: a table's verbose pipe source
    // has far more bytes than its compact frame occupies in the document, so
    // byte arithmetic overruns every element after a table — an invalid
    // QTextBlock then fed to QTextList::add segfaulted (2026-05-31). Instead,
    // walk the document's actual top-level elements (root-frame iterator;
    // it does NOT descend into table cells) in lockstep with the model
    // blocks.
    const std::vector<Markoff::BlockId> blocks = doc->iterateBlocks();

    QTextFrame *rootFrame = qdoc->rootFrame();
    QTextFrame::iterator docIt = rootFrame->begin();
    auto skipArtifactBlocks = [&]() {
        // Qt inserts an empty structural QTextBlock adjacent to a frame
        // (e.g. between a table frame and the following content block). Such
        // a block belongs to no model block; skip empty non-frame blocks.
        while (docIt != rootFrame->end()
               && !docIt.currentFrame()
               && docIt.currentBlock().isValid()
               && docIt.currentBlock().text().isEmpty()) {
            ++docIt;
        }
    };

    bool synced = true;
    for (size_t i = 0; i < blocks.size(); ++i) {
        WalkEntry entry;
        entry.blockId = blocks[i];
        entry.kind = doc->blockKind(entry.blockId);
        entry.text = doc->blockText(entry.blockId);

        // ── Table: consume the QTextTable frame. ──
        if (entry.kind == Markoff::BlockKind::Table) {
            entry.isFrame = true;
            skipArtifactBlocks();
            if (docIt != rootFrame->end() && docIt.currentFrame()) {
                entry.frame = docIt.currentFrame();
                ++docIt;  // step over the frame
            } else {
                synced = false;  // Table block with no frame at its position
            }
            visit(entry);
            continue;
        }

        // ── Non-table: consume the next (lineCount) top-level QTextBlocks. ──
        // The opaque-aware seed inserts each model block's content with its
        // internal newlines preserved, so a block with K internal '\n's
        // occupies K+1 consecutive top-level QTextBlocks.
        const int lineCount = entry.text.count('\n') + 1;
        skipArtifactBlocks();
        int consumed = 0;
        while (consumed < lineCount && docIt != rootFrame->end()) {
            if (docIt.currentFrame()) { ++docIt; continue; }  // defensive
            QTextBlock qblk = docIt.currentBlock();
            if (!qblk.isValid()) { ++docIt; continue; }
            if (!entry.firstQtBlock.isValid()) entry.firstQtBlock = qblk;
            entry.qtBlocks.append(qblk);
            ++docIt;
            ++consumed;
        }
        if (consumed < lineCount) synced = false;
        visit(entry);
    }
    return synced;
}

}  // namespace Markoff::Styled
