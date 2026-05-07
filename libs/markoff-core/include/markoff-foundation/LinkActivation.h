// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QUrl>

#include <markoff-foundation/LinkKind.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT LinkActivation {
    QString  rawText;
    QUrl     resolvedTarget;
    LinkKind kind = LinkKind::Unknown;
    QString  anchorHint;
    QString  fromContext;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::LinkActivation)
