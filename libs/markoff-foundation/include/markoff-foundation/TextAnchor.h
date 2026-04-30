// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// Opaque view-layer-safe wrapper for a CRDT byte anchor. Same wire-
/// level identity (replicaId + charValue + bias) as
/// CollabText::Crdt::Anchor; conversion via foundation-internal
/// helpers in AnchorConversion.h. Consumers may hold and pass; must
/// NOT inspect, construct from raw fields, or compare except via
/// operator==. All translations go through MarkoffDocument APIs.
struct MARKOFF_FOUNDATION_EXPORT TextAnchor {
    quint16 replicaId = 0;
    quint32 charValue = 0;
    quint8  bias      = 0;   ///< 0 = Left, 1 = Right (matches Crdt::Bias enum order)

    bool operator==(const TextAnchor &) const = default;
};

}  // namespace Markoff
