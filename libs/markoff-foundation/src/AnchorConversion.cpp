// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnchorConversion.h"

namespace Markoff::Detail {

TextAnchor toTextAnchor(const CollabText::Crdt::Anchor &a) noexcept
{
    TextAnchor t;
    t.replicaId = a.replica_id;
    t.charValue = a.char_value;
    t.bias      = (a.bias == CollabText::Crdt::Bias::Right) ? 1 : 0;
    return t;
}

CollabText::Crdt::Anchor toCrdtAnchor(const TextAnchor &t) noexcept
{
    using CollabText::Crdt::Bias;
    return CollabText::Crdt::Anchor{
        t.replicaId,
        t.charValue,
        t.bias == 1 ? Bias::Right : Bias::Left
    };
}

}  // namespace Markoff::Detail
