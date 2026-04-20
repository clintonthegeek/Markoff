// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/SearchAdapter.h>

namespace Markoff {

class Editor;

/// Bridges the live-preview Editor's existing search-highlight
/// machinery to the SearchAdapter interface owned by SearchController.
class LiveSearchAdapter : public SearchAdapter {
public:
    explicit LiveSearchAdapter(Editor *owner);

    int cursorSourceOffset() const override;
    void highlightMatches(QVector<TextSpan>) override;
    void clearMatchHighlight() override;
    void scrollMatchIntoView(TextSpan) override;

private:
    Editor *m_editor;
};

}  // namespace Markoff
