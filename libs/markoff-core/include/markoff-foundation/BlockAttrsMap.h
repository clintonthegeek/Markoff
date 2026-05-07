// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/CausalLwwMap.h>
#include <markoff-foundation/BlockId.h>
#include <QByteArray>
#include <QString>
#include <QHashFunctions>
#include <variant>

namespace Markoff {

using AttrName  = QByteArray;
using AttrValue = std::variant<int, QString, bool>;

struct BlockAttrKey {
    BlockId  block;
    AttrName name;
    bool operator==(const BlockAttrKey &) const = default;
};

inline size_t qHash(const BlockAttrKey &k, size_t seed) noexcept {
    return qHashMulti(seed, k.block, k.name);
}

using BlockAttrsMap = CausalLwwMap<BlockAttrKey, AttrValue>;

}  // namespace Markoff
