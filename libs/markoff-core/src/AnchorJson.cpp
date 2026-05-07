// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/AnchorJson.h>

namespace Markoff {

using CollabText::Crdt::Anchor;
using CollabText::Crdt::Bias;

QJsonObject anchorToJson(const Anchor &a)
{
    QJsonObject obj;
    obj.insert("rid", static_cast<qint64>(a.replica_id));
    obj.insert("cv",  static_cast<qint64>(a.char_value));
    obj.insert("bias", a.bias == Bias::Right ? "R" : "L");
    return obj;
}

Anchor anchorFromJson(const QJsonObject &obj)
{
    Anchor a;
    a.replica_id = static_cast<uint16_t>(obj.value("rid").toInteger());
    a.char_value = static_cast<uint32_t>(obj.value("cv").toInteger());
    a.bias = (obj.value("bias").toString() == QStringLiteral("R"))
        ? Bias::Right : Bias::Left;
    return a;
}

}  // namespace Markoff
