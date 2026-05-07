// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/CausalLwwMap.h>
#include <QByteArray>
#include <QString>

namespace Markoff {

struct LinkRefValue {
    QString url;
    QString title;
    bool operator==(const LinkRefValue &) const = default;
};

using LinkRefId  = QByteArray;
using LinkRefMap = CausalLwwMap<LinkRefId, LinkRefValue>;

}  // namespace Markoff
