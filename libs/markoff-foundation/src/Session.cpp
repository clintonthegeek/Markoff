// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Session.h>

#include <QUuid>

#include <markoff-foundation/MarkoffDocument.h>
#include "SessionPrivate.h"

namespace Markoff {

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
    if (cur.anchor.replica_id == sel.anchor.replica_id
        && cur.anchor.char_value == sel.anchor.char_value
        && cur.anchor.bias       == sel.anchor.bias
        && cur.active.replica_id == sel.active.replica_id
        && cur.active.char_value == sel.active.char_value
        && cur.active.bias       == sel.active.bias
        && cur.kind              == sel.kind)
    {
        return;
    }
    d->primary = sel;
    Q_EMIT primarySelectionChanged(d->primary);
}

const QList<Selection> &Session::secondarySelections() const { return d->secondaries; }
void Session::setSecondarySelections(QList<Selection>) {}
void Session::addSecondarySelection(Selection) {}
void Session::clearSecondarySelectionsOfKind(Selection::Kind) {}

CollabText::Crdt::Anchor Session::topVisibleAnchor() const { return d->topAnchor; }
qreal                    Session::topVisibleFraction() const { return d->topFraction; }
void Session::setTopVisible(CollabText::Crdt::Anchor, qreal) {}

const QList<FoldRef> &Session::foldedRegions() const { return d->folds; }
void Session::setFoldedRegions(QList<FoldRef>) {}
void Session::toggleFold(const FoldRef &) {}

void Session::copyStateFrom(const Session &) {}
QJsonObject Session::toJson() const { return {}; }
void        Session::fromJson(const QJsonObject &) {}

}  // namespace Markoff
