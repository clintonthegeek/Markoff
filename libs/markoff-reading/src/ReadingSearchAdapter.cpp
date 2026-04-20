// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReadingSearchAdapter.h"

#include "markoff/reading/ReadingView.h"

namespace Markoff::Reading {

int ReadingSearchAdapter::cursorSourceOffset() const
{
    // Phase A stub — no cursor in Reading mode. A later revision can
    // return the source-offset of the top-of-viewport section.
    return 0;
}

void ReadingSearchAdapter::highlightMatches(QVector<Markoff::TextSpan>)
{
    // Phase A stub.
}

void ReadingSearchAdapter::clearMatchHighlight()
{
    // Phase A stub.
}

void ReadingSearchAdapter::scrollMatchIntoView(Markoff::TextSpan)
{
    // Phase A stub.
}

}  // namespace Markoff::Reading
