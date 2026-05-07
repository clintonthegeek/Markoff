// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/BlockKind.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <QByteArray>
#include <QHash>

namespace Markoff {

class MARKOFF_CORE_EXPORT BlockSerializerRegistry {
public:
    virtual ~BlockSerializerRegistry() = default;
    virtual QByteArray serialize(BlockKind kind,
                                 const QHash<AttrName, AttrValue> &attrs,
                                 const QByteArray &content) const = 0;
};

}  // namespace Markoff
