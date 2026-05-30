// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <vector>

#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>

class QTextDocument;

namespace Markoff {
class MarkoffDocument;
class Theme;
}

namespace Markoff::Styled::FormatPass {

/// Per-call configuration.
struct Options {
    qreal                 fontScale = 1.0;
    const Markoff::Theme *theme     = nullptr;
    bool                  inferKind = false;  ///< false => no kind suggestions
};

/// A "this block's prefix disagrees with its stored kind" suggestion. The
/// caller (StyleApplier) decides whether to act (issue Cmd::changeKind);
/// FormatPass never mutates the model.
struct KindSuggestion {
    Markoff::BlockId   id;
    Markoff::BlockKind newKind;
};

using BlockHashMap = QHash<Markoff::BlockId, quint64>;

struct Result {
    quint64                     hashSkips  = 0;
    bool                        structural = false;  ///< block set changed vs gate
    std::vector<KindSuggestion> kindSuggestions;     ///< empty unless inferKind
};

/// Apply block + inline formats to `target` (which must already hold
/// `source->widgetFlatView()` text). PURE w.r.t. the model: issues no Cmd::*,
/// never mutates `source`. When `gate` is non-null, per-block hash gating is
/// applied and the map is updated + pruned; when null, every block is
/// formatted and no gate state is touched. Coalesces edits via
/// begin/endEditBlock and blocks `target`'s signals for the duration.
Result apply(QTextDocument *target,
             const Markoff::MarkoffDocument *source,
             const Options &opts,
             BlockHashMap *gate);

}  // namespace Markoff::Styled::FormatPass
