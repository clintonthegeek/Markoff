// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/EphemeralState.h>

#include <QJsonArray>

namespace Markoff {

QJsonObject EphemeralState::toJson() const
{
    QJsonObject j;
    j.insert(QStringLiteral("scroll"), scroll);
    j.insert(QStringLiteral("cursorLine"), cursor.line);
    j.insert(QStringLiteral("cursorColumn"), cursor.column);
    j.insert(QStringLiteral("mode"), viewMode);

    QJsonArray folds;
    for (const FoldSpec &f : foldedHeadings) {
        folds.append(QJsonObject{
            {QStringLiteral("line"), f.line},
            {QStringLiteral("level"), f.level},
        });
    }
    j.insert(QStringLiteral("folded"), folds);

    j.insert(QStringLiteral("extras"), extras);
    return j;
}

EphemeralState EphemeralState::fromJson(const QJsonObject &j)
{
    EphemeralState s;
    s.scroll = static_cast<float>(j.value(QStringLiteral("scroll")).toDouble(0.0));
    s.cursor.line = j.value(QStringLiteral("cursorLine")).toInt(0);
    s.cursor.column = j.value(QStringLiteral("cursorColumn")).toInt(0);
    s.viewMode = j.value(QStringLiteral("mode")).toString();

    for (const QJsonValue &v : j.value(QStringLiteral("folded")).toArray()) {
        const QJsonObject o = v.toObject();
        s.foldedHeadings.append(FoldSpec{
            o.value(QStringLiteral("line")).toInt(0),
            o.value(QStringLiteral("level")).toInt(0),
        });
    }

    s.extras = j.value(QStringLiteral("extras")).toObject();
    return s;
}

}  // namespace Markoff
