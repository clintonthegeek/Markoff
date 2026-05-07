// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/BlockAttrsMap.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <QByteArray>
#include <QHash>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT BlockSerializerRegistry {
public:
    virtual ~BlockSerializerRegistry() = default;
    virtual QByteArray serialize(BlockKind kind,
                                 const QHash<AttrName, AttrValue> &attrs,
                                 const QByteArray &content) const = 0;
};

}  // namespace Markoff
