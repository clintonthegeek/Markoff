// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/SearchAdapter.h>

namespace Markoff::Source {

class SourceEditor;

class SourceSearchAdapter : public Markoff::SearchAdapter {
public:
    explicit SourceSearchAdapter(SourceEditor *owner);

    int cursorSourceOffset() const override;
    void highlightMatches(QVector<Markoff::TextSpan>) override;
    void clearMatchHighlight() override;
    void scrollMatchIntoView(Markoff::TextSpan) override;

private:
    SourceEditor *m_editor;
};

}  // namespace Markoff::Source
