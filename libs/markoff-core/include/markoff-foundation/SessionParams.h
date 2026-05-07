// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QString>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// Construction-time parameters for a Session. Empty participantId means
/// the session is local-only (no CRDT presence broadcast).
struct MARKOFF_FOUNDATION_EXPORT SessionParams {
    QString participantId;
    QString participantLabel;
    QColor  presenceColor;
};

}  // namespace Markoff
