// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <crdt/Anchor.h>

#include <markoff-foundation/FoldRef.h>
#include <markoff-foundation/Selection.h>

namespace Markoff {

class MarkoffDocument;

struct Session::Private {
    MarkoffDocument *doc = nullptr;
    QString          id;
    QString          participantId;
    QString          participantLabel;
    QColor           presenceColor;

    Selection                primary;
    QList<Selection>         secondaries;
    CollabText::Crdt::Anchor topAnchor;
    qreal                    topFraction = 0.0;
    QList<FoldRef>           folds;
};

}  // namespace Markoff
