// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Session.h>

#include <algorithm>

#include <QJsonArray>
#include <QUuid>

#include <array>
#include <algorithm>
#include <markoff/core/MarkoffDocument.h>
#include "SessionPrivate.h"

namespace Markoff {

namespace {
QJsonObject positionToJson(const ScopedTextPosition &position)
{
    QJsonObject encoded;
    const QByteArray actor(reinterpret_cast<const char *>(position.block.actorBytes().data()),
                           qsizetype(position.block.actorBytes().size()));
    encoded.insert("version", 1);
    encoded.insert("actor", QString::fromLatin1(actor.toHex()));
    encoded.insert("counter", QString::number(position.block.counter()));
    encoded.insert("token", QString::fromLatin1(position.boundaryToken.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    return encoded;
}

std::optional<ScopedTextPosition> positionFromJson(const QJsonValue &value)
{
    if (!value.isObject()) return std::nullopt;
    const auto encoded = value.toObject();
    const QByteArray actorHex = encoded.value("actor").toString().toLatin1();
    const QByteArray actor = QByteArray::fromHex(actorHex);
    if (encoded.value("version").toInt() != 1 || actor.size() != 16
        || actor.toHex() != actorHex) return std::nullopt;
    std::array<uint8_t, 16> actorBytes{};
    std::copy(actor.cbegin(), actor.cend(), actorBytes.begin());
    bool counterOk = false;
    const auto counter = encoded.value("counter").toString().toULongLong(&counterOk);
    const auto block = counterOk ? BlockId::fromStructuralIdentity(actorBytes, counter)
                                 : std::optional<BlockId>{};
    const QByteArray tokenText = encoded.value("token").toString().toLatin1();
    const QByteArray token = QByteArray::fromBase64(tokenText,
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!block || token.isEmpty() || token.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != tokenText)
        return std::nullopt;
    return ScopedTextPosition{*block, token};
}
}

Session::Session(MarkoffDocument *doc, const SessionParams &params)
    : QObject(doc)
    , d(std::make_unique<Private>())
{
    d->doc = doc;
    d->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    d->participantId    = params.participantId;
    d->participantLabel = params.participantLabel;
    d->presenceColor    = params.presenceColor;
}

Session::~Session() = default;

QString Session::id() const               { return d->id; }
QString Session::participantId() const    { return d->participantId; }
QString Session::participantLabel() const { return d->participantLabel; }
QColor  Session::presenceColor() const    { return d->presenceColor; }

// Selection / scroll / fold getters return defaults until Tasks 19-22.
Selection Session::primarySelection() const { return d->primary; }

void Session::setPrimarySelection(const Selection &sel)
{
    const Selection &cur = d->primary;
    if (cur.anchorPosition == sel.anchorPosition
        && cur.activePosition == sel.activePosition
        && cur.kind == sel.kind)
    {
        return;
    }
    d->primary = sel;
    Q_EMIT primarySelectionChanged(d->primary);
}

const QList<Selection> &Session::secondarySelections() const { return d->secondaries; }
void Session::setSecondarySelections(QList<Selection> sels)
{
    d->secondaries = std::move(sels);
    Q_EMIT secondarySelectionsChanged();
}

void Session::addSecondarySelection(Selection sel)
{
    d->secondaries.append(std::move(sel));
    Q_EMIT secondarySelectionsChanged();
}

void Session::clearSecondarySelectionsOfKind(Selection::Kind kind)
{
    QList<Selection> kept;
    kept.reserve(d->secondaries.size());
    for (const Selection &s : d->secondaries) {
        if (s.kind != kind) kept << s;
    }
    if (kept.size() == d->secondaries.size()) return;
    d->secondaries = std::move(kept);
    Q_EMIT secondarySelectionsChanged();
}

std::optional<ScopedTextPosition> Session::topVisiblePosition() const { return d->topPosition; }
qreal                    Session::topVisibleFraction() const { return d->topFraction; }

void Session::setTopVisible(ScopedTextPosition position, qreal fraction)
{
    d->topPosition = std::move(position);
    d->topFraction = fraction;
    Q_EMIT scrollPositionChanged(*d->topPosition, fraction);
}

const QList<FoldRef> &Session::foldedRegions() const { return d->folds; }

void Session::setFoldedRegions(QList<FoldRef> folds)
{
    d->folds = std::move(folds);
    Q_EMIT foldedRegionsChanged();
}

void Session::toggleFold(const FoldRef &f)
{
    // Match by block-qualified text position — ignores
    // ephemeral heading-path drift across parses.
    const auto matches = [&](const FoldRef &x) {
        return x.startPosition == f.startPosition
            && x.kind == f.kind;
    };
    const auto it = std::find_if(d->folds.begin(), d->folds.end(), matches);
    if (it == d->folds.end()) {
        d->folds.append(f);
    } else {
        d->folds.erase(it);
    }
    Q_EMIT foldedRegionsChanged();
}

void Session::copyStateFrom(const Session &other)
{
    d->primary     = other.d->primary;
    d->secondaries = other.d->secondaries;
    d->topPosition = other.d->topPosition;
    d->topFraction = other.d->topFraction;
    d->folds       = other.d->folds;
    Q_EMIT primarySelectionChanged(d->primary);
    Q_EMIT secondarySelectionsChanged();
    if (d->topPosition) Q_EMIT scrollPositionChanged(*d->topPosition, d->topFraction);
    Q_EMIT foldedRegionsChanged();
}

QJsonObject Session::toJson() const
{
    QJsonObject obj;
    obj.insert("id",               d->id);
    obj.insert("participantId",    d->participantId);
    obj.insert("participantLabel", d->participantLabel);
    obj.insert("presenceColor",    d->presenceColor.name(QColor::HexArgb));
    obj.insert("primary",          d->primary.toJson());

    QJsonArray sec;
    for (const Selection &s : d->secondaries) sec.append(s.toJson());
    obj.insert("secondaries", sec);

    if (d->topPosition) obj.insert("topPosition", positionToJson(*d->topPosition));
    obj.insert("topFraction", d->topFraction);

    QJsonArray folds;
    for (const FoldRef &f : d->folds) folds.append(f.toJson());
    obj.insert("folds", folds);
    return obj;
}

void Session::fromJson(const QJsonObject &obj)
{
    d->id               = obj.value("id").toString();
    d->participantId    = obj.value("participantId").toString();
    d->participantLabel = obj.value("participantLabel").toString();
    d->presenceColor    = QColor(obj.value("presenceColor").toString());
    d->primary          = Selection::fromJson(obj.value("primary").toObject());

    d->secondaries.clear();
    for (const QJsonValue &v : obj.value("secondaries").toArray())
        d->secondaries << Selection::fromJson(v.toObject());

    d->topPosition = positionFromJson(obj.value("topPosition"));
    d->topFraction = obj.value("topFraction").toDouble();

    d->folds.clear();
    for (const QJsonValue &v : obj.value("folds").toArray())
        d->folds << FoldRef::fromJson(v.toObject());

    Q_EMIT primarySelectionChanged(d->primary);
    Q_EMIT secondarySelectionsChanged();
    if (d->topPosition) Q_EMIT scrollPositionChanged(*d->topPosition, d->topFraction);
    Q_EMIT foldedRegionsChanged();
}

}  // namespace Markoff
