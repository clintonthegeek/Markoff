// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/SearchAdapter.h>

namespace Markoff::Reading {

class ReadingView;

/// SearchAdapter for Reading mode. Reading is read-only by design, so
/// `supportsReplace()` returns false — ReplaceController checks this
/// and refuses to mutate.
///
/// Phase A: highlight/scroll operations are no-op stubs pending a later
/// wiring to ReadingView's per-section highlight pass.
class ReadingSearchAdapter : public Markoff::SearchAdapter {
public:
    explicit ReadingSearchAdapter(ReadingView *owner) : m_owner(owner) {}
    ~ReadingSearchAdapter() override = default;

    int cursorSourceOffset() const override;
    void highlightMatches(QVector<Markoff::TextSpan>) override;
    void clearMatchHighlight() override;
    void scrollMatchIntoView(Markoff::TextSpan) override;
    bool supportsReplace() const override { return false; }

private:
    ReadingView *m_owner = nullptr;
};

}  // namespace Markoff::Reading
