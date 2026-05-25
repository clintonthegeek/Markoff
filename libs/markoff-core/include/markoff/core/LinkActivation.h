// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QUrl>
#include <QtCore/qnamespace.h>

#include <markoff/core/LinkKind.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

struct MARKOFF_CORE_EXPORT LinkActivation {
    QString  rawText;
    QUrl     resolvedTarget;
    LinkKind kind = LinkKind::Unknown;
    QString  anchorHint;
    QString  fromContext;

    // E3a additions: structured wikilink fields + click modifiers.
    QString  page;
    QString  section;
    QString  blockRef;
    QString  alias;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::LinkActivation)
