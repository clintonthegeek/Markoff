// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
//
// Obsidian-documented reading-pipeline contract constants. These numbers are
// not hints; they are the documented wire contract. The values must be
// honoured exactly — `tst_frame_budget_constants` pins them.

#ifndef CORBOMITE_READINGVIEW_READINGVIEWCONSTANTS_H
#define CORBOMITE_READINGVIEW_READINGVIEWCONSTANTS_H

namespace Corbomite::ReadingView {

/// Notes at or above this size parse off the main thread (ReadingParseWorker).
/// Smaller notes parse synchronously on the calling thread.
constexpr int kAsyncParseThresholdBytes = 10240;

/// Per-frame render-phase wall-time budget. The mount loop yields to the
/// event loop once this is reached or exceeded.
constexpr int kFrameBudgetMs = 5;

/// Per-frame render-phase section-count budget. The mount loop yields to the
/// event loop once this many sections have been laid out in the current
/// frame, even if the wall-time budget is not yet spent.
constexpr int kFrameBudgetSections = 10;

/// `QGraphicsItem::setData()` key under which Phase 6's heading-fold gutter
/// arrow carries its owning section index. Shared between `SectionLayout`
/// (which stamps) and `ReadingView` (which hit-tests). The value is the
/// hex literal for ASCII "FOLD".
constexpr int kFoldArrowSectionIdxProperty = 0x464F4C44;

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_READINGVIEWCONSTANTS_H
