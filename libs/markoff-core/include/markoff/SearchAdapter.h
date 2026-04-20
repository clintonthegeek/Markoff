// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QVector>

#include <markoff/TextSpan.h>

namespace Markoff {

/// Each MarkdownView subclass provides one of these to SearchController
/// so the engine can highlight and scroll matches without knowing
/// anything about the view internals.
class SearchAdapter {
public:
    virtual ~SearchAdapter() = default;

    /// Where should "find from cursor" start? Source offset into the
    /// owning MarkoffDocument's plainText(). Read-only views return
    /// a sensible default (e.g. their scroll-top source offset).
    virtual int cursorSourceOffset() const = 0;

    /// Paint highlights for every match. May be called with an empty
    /// vector to request all highlights cleared.
    virtual void highlightMatches(QVector<TextSpan>) = 0;

    /// Explicit clear hook. SearchController calls this when the
    /// query is empty, before deactivation.
    virtual void clearMatchHighlight() = 0;

    /// Scroll the view so the match is visible. Called on next()/prev()
    /// and on query change if there is at least one match.
    virtual void scrollMatchIntoView(TextSpan) = 0;

    /// False for Reading-like read-only views. ReplaceController
    /// checks this and refuses to mutate when false.
    virtual bool supportsReplace() const { return true; }
};

}  // namespace Markoff
