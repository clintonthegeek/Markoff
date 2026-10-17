// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Selection.h>

#include <array>
#include <algorithm>

namespace Markoff {

bool Selection::isEmpty() const
{
    if (anchorPosition && activePosition)
        return *anchorPosition == *activePosition;
    return true;
}

namespace {
const char *kindToString(Selection::Kind k)
{
    switch (k) {
    case Selection::Kind::Primary:     return "primary";
    case Selection::Kind::Secondary:   return "secondary";
    case Selection::Kind::SearchMatch: return "searchMatch";
    case Selection::Kind::Presence:    return "presence";
    }
    return "primary";
}

Selection::Kind kindFromString(const QString &s)
{
    if (s == QStringLiteral("secondary"))   return Selection::Kind::Secondary;
    if (s == QStringLiteral("searchMatch")) return Selection::Kind::SearchMatch;
    if (s == QStringLiteral("presence"))    return Selection::Kind::Presence;
    return Selection::Kind::Primary;
}
}  // namespace

QJsonObject Selection::toJson() const
{
    QJsonObject obj;
    const auto positionToJson = [](const ScopedTextPosition& position) {
        QJsonObject encoded;
        QByteArray actor(reinterpret_cast<const char*>(position.block.actorBytes().data()),
                         qsizetype(position.block.actorBytes().size()));
        encoded.insert("version", 1);
        encoded.insert("actor", QString::fromLatin1(actor.toHex()));
        encoded.insert("counter", QString::number(position.block.counter()));
        encoded.insert("token", QString::fromLatin1(position.boundaryToken.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
        return encoded;
    };
    if (anchorPosition) obj.insert("anchorPosition", positionToJson(*anchorPosition));
    if (activePosition) obj.insert("activePosition", positionToJson(*activePosition));
    obj.insert("kind", QString::fromLatin1(kindToString(kind)));
    if (kind == Kind::Presence) {
        obj.insert("participantId", participantId);
        obj.insert("participantLabel", participantLabel);
        obj.insert("presenceColor", presenceColor.name(QColor::HexArgb));
        obj.insert("cursorVersion", static_cast<qint64>(cursorVersion));
    }
    return obj;
}

Selection Selection::fromJson(const QJsonObject &obj)
{
    Selection s;
    const auto positionFromJson = [](const QJsonValue& value)
        -> std::optional<ScopedTextPosition> {
        if (!value.isObject()) return std::nullopt;
        const auto encoded = value.toObject();
        if (encoded.value("version").toInt() != 1) return std::nullopt;
        const QByteArray actorHex = encoded.value("actor").toString().toLatin1();
        const QByteArray actor = QByteArray::fromHex(actorHex);
        if (actor.size() != 16 || actor.toHex() != actorHex) return std::nullopt;
        std::array<uint8_t, 16> actorBytes{};
        std::copy(actor.cbegin(), actor.cend(), actorBytes.begin());
        bool counterOk = false;
        const auto counter = encoded.value("counter").toString().toULongLong(&counterOk);
        const auto block = counterOk
            ? BlockId::fromStructuralIdentity(actorBytes, counter)
            : std::optional<BlockId>{};
        if (!block) return std::nullopt;
        const QByteArray tokenText = encoded.value("token").toString().toLatin1();
        const QByteArray token = QByteArray::fromBase64(tokenText,
            QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
        if (token.isEmpty() || token.toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
                != tokenText)
            return std::nullopt;
        return ScopedTextPosition{*block, token};
    };
    if (obj.contains("anchorPosition"))
        s.anchorPosition = positionFromJson(obj.value("anchorPosition"));
    if (obj.contains("activePosition"))
        s.activePosition = positionFromJson(obj.value("activePosition"));
    s.kind = kindFromString(obj.value("kind").toString());
    if (s.kind == Kind::Presence) {
        s.participantId = obj.value("participantId").toString();
        s.participantLabel = obj.value("participantLabel").toString();
        s.presenceColor = QColor(obj.value("presenceColor").toString());
        s.cursorVersion = static_cast<quint64>(obj.value("cursorVersion").toInteger());
    }
    return s;
}

}  // namespace Markoff
