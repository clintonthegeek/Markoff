// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/BlockId.h>

namespace Markoff {

class MarkoffDocument;

struct Watermark {
    quint64 idListVersion   = 0;
    quint64 kindTagMapSeq   = 0;
    quint64 blockAttrsSeq   = 0;
    quint64 frontmatterSeq  = 0;
    quint64 linkRefSeq      = 0;
    quint64 footnoteDefSeq  = 0;
};

class MARKOFF_CORE_EXPORT WatermarkCoordinator {
public:
    explicit WatermarkCoordinator(MarkoffDocument &doc);
    bool onSaveSucceeded();
    Watermark currentWatermark() const;

    /// Run compaction immediately (without gate). Returns false if a
    /// transaction is open or watermark is null.
    bool compactNow();

private:
    void advanceAndCompact();
    void disposeOrphans();
    MarkoffDocument &m_doc;
    Watermark m_watermark;
};

}  // namespace Markoff
