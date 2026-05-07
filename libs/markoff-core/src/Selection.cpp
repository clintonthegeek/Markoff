// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Selection.h>

#include <markoff-foundation/AnchorJson.h>
#include <markoff-foundation/BlockId.h>

#include "AnchorConversion.h"

namespace Markoff {

bool Selection::isEmpty() const
{
    return anchor == active;
}

bool Selection::isReversed() const
{
    // Without a Buffer to resolve to byte offsets, we can't deterministically
    // order anchors. This returns whether the cursor head sits "before" the
    // anchor in lexicographic (charValue, replicaId) order — a coarse
    // proxy. Callers that need byte-offset semantics should resolve via the
    // Buffer and compare byte offsets directly.
    if (active.charValue() != anchor.charValue())
        return active.charValue() < anchor.charValue();
    return active.replicaId() < anchor.replicaId();
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
    obj.insert("anchor", anchorToJson(Detail::toCrdtAnchor(anchor)));
    obj.insert("active", anchorToJson(Detail::toCrdtAnchor(active)));
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
    s.anchor = Detail::toTextAnchor(BlockId{}, anchorFromJson(obj.value("anchor").toObject()));
    s.active = Detail::toTextAnchor(BlockId{}, anchorFromJson(obj.value("active").toObject()));
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
