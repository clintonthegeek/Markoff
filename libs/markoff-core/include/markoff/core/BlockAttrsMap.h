// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/CausalLwwMap.h>
#include <markoff/core/BlockId.h>
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

// seed defaults to 0 so Qt 6.9+ qHashMulti (1-arg qHash calls) still compiles.
inline size_t qHash(const BlockAttrKey &k, size_t seed = 0) noexcept {
    return qHashMulti(seed, k.block, k.name);
}

using BlockAttrsMap = CausalLwwMap<BlockAttrKey, AttrValue>;

}  // namespace Markoff
