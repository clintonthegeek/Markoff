// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

namespace Markoff {

/// Metadata accompanying a structured-paste invocation. Tells the document
/// whether this paste is a re-paste of a recent local cut (in which case
/// the original BlockIds may be reused), or a fresh paste (new IDs minted).
struct PasteMeta {
    bool    reuseBlockIds = false;
    quint64 cutSeq        = 0;  ///< matched cutSequenceNumber; 0 if reuseBlockIds is false
};

}  // namespace Markoff
