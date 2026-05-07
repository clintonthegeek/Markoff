// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnchorConversion.h"

namespace Markoff::Detail {

TextAnchor toTextAnchor(BlockId blockId, const CollabText::Crdt::Anchor &a) noexcept
{
    return TextAnchor::make(blockId,
                            a.replica_id,
                            a.char_value,
                            a.bias == CollabText::Crdt::Bias::Right);
}

CollabText::Crdt::Anchor toCrdtAnchor(const TextAnchor &t) noexcept
{
    using CollabText::Crdt::Bias;
    return CollabText::Crdt::Anchor{
        t.replicaId(),
        static_cast<quint32>(t.charValue()),
        t.rightBias() ? Bias::Right : Bias::Left
    };
}

}  // namespace Markoff::Detail
