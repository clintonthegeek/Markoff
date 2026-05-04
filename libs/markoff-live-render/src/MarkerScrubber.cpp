// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/MarkerScrubber.h>

#include <markoff/live-render/LiveBlockModel.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include <optional>

namespace {

/// Byte range [start, end) of the marker paragraph at `blockIndex` PLUS
/// its leading "\n\n" separator (or preceding "\n" if it is the first
/// paragraph). Returns std::nullopt if the row is out of range or is not
/// marker-only at the time of the call.
struct ScrubRange { quint32 start; quint32 end; };

std::optional<ScrubRange> markerScrubRangeFor(int blockIndex,
                                               Markoff::LiveRender::LiveBlockModel *model,
                                               Markoff::MarkoffDocument *doc)
{
    if (!model || !doc) return std::nullopt;
    if (blockIndex < 0 || blockIndex >= model->rowCount()) return std::nullopt;
    const auto &rec = model->recordAt(blockIndex);
    if (!Markoff::LiveRender::MarkerScrubber::isMarkerOnly(rec.text))
        return std::nullopt;
    auto range = doc->blockByteRange(rec.blockAnchor);
    if (!range) return std::nullopt;
    quint32 start = range->first;
    quint32 end   = range->second;
    // Extend `start` backwards by 1 byte to absorb the inter-block gap
    // "\n" that precedes the marker paragraph. Each block's byte range
    // already includes the block's own trailing "\n"; the single gap
    // byte is the only extra newline to reclaim. The first paragraph in
    // the document has no preceding gap, so we clamp to 0.
    quint32 desiredAbsorb = (start >= 1) ? 1 : 0;
    start -= desiredAbsorb;
    return ScrubRange{ start, end };
}

}  // namespace

namespace Markoff::LiveRender {

MarkerScrubber::MarkerScrubber(Markoff::MarkoffDocument *doc,
                                LiveBlockModel           *model,
                                QObject                  *parent)
    : QObject(parent), m_doc(doc), m_model(model)
{}

bool MarkerScrubber::isMarkerOnly(const QString &text) {
    if (text.isEmpty()) return false;
    for (QChar c : text) {
        if (c == kMarkerChar) continue;
        if (c == QChar('\n')) continue;  // soft-break, per spec §17 q1
        return false;
    }
    return true;
}

void MarkerScrubber::scrubOnFocusOut(int blockIndex) {
    if (!m_doc || !m_model) return;
    auto range = markerScrubRangeFor(blockIndex, m_model, m_doc);
    if (!range) return;
    Markoff::MarkoffEdit ed;
    ed.oldStart = range->start;
    ed.oldEnd   = range->end;
    ed.newText  = QByteArray();
    m_doc->applyLocalEdit({ ed });
}

int MarkerScrubber::scrubBeforeSave() {
    if (!m_doc || !m_model) return 0;
    // Walk paragraphs in REVERSE order so each scrub edit's byte
    // arithmetic doesn't shift the indices of paragraphs we haven't
    // visited yet. (Forward order would require recomputing every
    // subsequent block's range after each edit.)
    int totalRemoved = 0;
    for (int i = m_model->rowCount() - 1; i >= 0; --i) {
        auto range = markerScrubRangeFor(i, m_model, m_doc);
        if (!range) continue;
        Markoff::MarkoffEdit ed;
        ed.oldStart = range->start;
        ed.oldEnd   = range->end;
        ed.newText  = QByteArray();
        m_doc->applyLocalEdit({ ed });
        totalRemoved += int(range->end - range->start);
    }
    return totalRemoved;
}

int MarkerScrubber::scrubAfterLoad() {
    return scrubBeforeSave();
}

}  // namespace Markoff::LiveRender
