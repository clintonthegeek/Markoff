// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include <QByteArray>
#include <QList>
#include <QTextBlock>

#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>

class QTextDocument;
class QTextFrame;

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::Styled {

/// Frame-aware lockstep walk: visits each model block with its
/// corresponding QTextDocument top-level element(s). A Table block maps
/// to its QTextTable frame (`isFrame == true`, `firstQtBlock` invalid);
/// a text block with K internal '\n's maps to the K+1 consecutive
/// top-level QTextBlocks the opaque-aware seed inserted for it. The walk
/// uses the root QTextFrame::iterator (which does NOT descend into table
/// cells) and skips Qt's empty artifact blocks adjacent to frames —
/// positions must never be derived from flat pipe-source bytes (the
/// 2026-05-31 SIGSEGV class). Extracted from FormatPass so find / future
/// consumers cannot drift from the rendering walk. MUST stay
/// behavior-identical to the FormatPass walk — FormatPass consumes this
/// same iterator.
struct WalkEntry {
    Markoff::BlockId   blockId;
    Markoff::BlockKind kind;            ///< model kind (drives frame-vs-text path)
    QByteArray         text;            ///< doc->blockText(blockId), fetched by the walk
    bool               isFrame = false; ///< true for Table blocks
    QTextFrame        *frame = nullptr; ///< consumed frame; null when !isFrame or desynced
    QTextBlock         firstQtBlock;    ///< first consumed QTextBlock; invalid when isFrame or desynced
    QList<QTextBlock>  qtBlocks;        ///< every top-level QTextBlock this model block spans
};

/// Calls visit(entry) for each model block in order. Visits EVERY model
/// block even when the document structure desynced (an entry then has an
/// invalid firstQtBlock / null frame / short qtBlocks — defensive,
/// matches FormatPass, whose hash gate and kind inference must run for
/// every block regardless). Returns false if any desync was observed
/// (a Table block without a frame at its position, or the document ran
/// out of QTextBlocks before a text block's line count was consumed).
///
/// visit may apply formats to the consumed blocks (format-only changes
/// don't perturb the frame iterator) but must not insert or remove
/// document content.
bool walkBlocks(const Markoff::MarkoffDocument *doc, QTextDocument *qdoc,
                const std::function<void(const WalkEntry &)> &visit);

}  // namespace Markoff::Styled
