// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/FoldRef.h>

#include <QJsonArray>

#include <markoff-foundation/AnchorJson.h>

namespace Markoff {

QJsonObject FoldRef::toJson() const
{
    QJsonObject obj;
    obj.insert("kind", kind == Kind::Block ? "block" : "heading");
    obj.insert("start", anchorToJson(start));
    if (kind == Kind::Heading) {
        QJsonArray path;
        for (const QString &p : headingPath)
            path.append(p);
        obj.insert("headingPath", path);
        obj.insert("headingLevel", headingLevel);
    }
    return obj;
}

FoldRef FoldRef::fromJson(const QJsonObject &obj)
{
    FoldRef f;
    f.kind = obj.value("kind").toString() == QStringLiteral("block")
        ? Kind::Block : Kind::Heading;
    f.start = anchorFromJson(obj.value("start").toObject());
    if (f.kind == Kind::Heading) {
        const QJsonArray path = obj.value("headingPath").toArray();
        for (const QJsonValue &v : path)
            f.headingPath << v.toString();
        f.headingLevel = obj.value("headingLevel").toInt();
    }
    return f;
}

}  // namespace Markoff
