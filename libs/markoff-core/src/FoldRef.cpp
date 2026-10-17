// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/FoldRef.h>

#include <QJsonArray>

#include <array>
#include <algorithm>

namespace Markoff {

QJsonObject FoldRef::toJson() const
{
    QJsonObject obj;
    obj.insert("kind", kind == Kind::Block ? "block" : "heading");
    if (startPosition) {
        QByteArray actor(reinterpret_cast<const char*>(startPosition->block.actorBytes().data()),
                         qsizetype(startPosition->block.actorBytes().size()));
        QJsonObject position;
        position.insert("version", 1);
        position.insert("actor", QString::fromLatin1(actor.toHex()));
        position.insert("counter", QString::number(startPosition->block.counter()));
        position.insert("token", QString::fromLatin1(startPosition->boundaryToken.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
        obj.insert("startPosition", position);
    }
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
    if (obj.contains("startPosition")) {
        const auto position = obj.value("startPosition").toObject();
        const QByteArray actorHex = position.value("actor").toString().toLatin1();
        const QByteArray actor = QByteArray::fromHex(actorHex);
        bool counterOk = false;
        const auto counter = position.value("counter").toString().toULongLong(&counterOk);
        if (position.value("version").toInt() == 1 && actor.size() == 16
            && actor.toHex() == actorHex && counterOk) {
            std::array<uint8_t, 16> actorBytes{};
            std::copy(actor.cbegin(), actor.cend(), actorBytes.begin());
            const auto block = BlockId::fromStructuralIdentity(actorBytes, counter);
            const QByteArray tokenText = position.value("token").toString().toLatin1();
            const QByteArray token = QByteArray::fromBase64(tokenText,
                QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
            if (block && !token.isEmpty() && token.toBase64(
                    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
                    == tokenText)
                f.startPosition = ScopedTextPosition{*block, token};
        }
    }
    if (f.kind == Kind::Heading) {
        const QJsonArray path = obj.value("headingPath").toArray();
        for (const QJsonValue &v : path)
            f.headingPath << v.toString();
        f.headingLevel = obj.value("headingLevel").toInt();
    }
    return f;
}

}  // namespace Markoff
