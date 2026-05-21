// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaType>
#include <QtGlobal>

namespace Markoff::Live {

/// A single find-match range within a block's text. Byte offsets are
/// relative to the block's current text (UTF-8), matching the units used
/// by `Markoff::FindController::Match::byteOffset` /
/// `Markoff::SearchEngine::SearchHit::matchStart`.
///
/// `isCurrent` is true for the one match that `FindController` reports as
/// `currentMatchIndex`; all other matches in the block (and across all
/// blocks) have `isCurrent = false`.
struct FindSpan {
    quint32 byteOffset = 0;
    quint32 byteLength = 0;
    bool    isCurrent  = false;

    bool operator==(const FindSpan &o) const noexcept {
        return byteOffset == o.byteOffset
            && byteLength == o.byteLength
            && isCurrent  == o.isCurrent;
    }
    bool operator!=(const FindSpan &o) const noexcept { return !(*this == o); }
};

}  // namespace Markoff::Live

Q_DECLARE_METATYPE(Markoff::Live::FindSpan)
