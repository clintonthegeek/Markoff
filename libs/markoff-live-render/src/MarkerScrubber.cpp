// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/MarkerScrubber.h>

#include <markoff/live-render/LiveBlockModel.h>
#include <markoff-foundation/MarkoffDocument.h>

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

void MarkerScrubber::scrubOnFocusOut(int /*blockIndex*/) {
    // Implemented in Task 3.
}

int MarkerScrubber::scrubBeforeSave() {
    // Implemented in Task 3.
    return 0;
}

int MarkerScrubber::scrubAfterLoad() {
    // Implemented in Task 3.
    return 0;
}

}  // namespace Markoff::LiveRender
