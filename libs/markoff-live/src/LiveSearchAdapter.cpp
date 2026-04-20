// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveSearchAdapter.h"

#include <markoff/Editor.h>

namespace Markoff {

LiveSearchAdapter::LiveSearchAdapter(Editor *owner) : m_editor(owner) {}

int LiveSearchAdapter::cursorSourceOffset() const
{
    // Phase A: approximate by asking the Editor for its current
    // source-line offset. Full source-offset fidelity lands in
    // Phase C when block items expose their source ranges through
    // the shared document.
    return m_editor ? m_editor->sourceOffsetAtCursor() : 0;
}

void LiveSearchAdapter::highlightMatches(QVector<TextSpan> spans)
{
    if (!m_editor) return;
    m_editor->highlightSearchSpans(spans);
}

void LiveSearchAdapter::clearMatchHighlight()
{
    if (!m_editor) return;
    m_editor->clearSearchHighlightsPublic();
}

void LiveSearchAdapter::scrollMatchIntoView(TextSpan span)
{
    if (!m_editor) return;
    m_editor->scrollSourceSpanIntoView(span);
}

}  // namespace Markoff
