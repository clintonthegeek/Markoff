// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <markoff/CursorPos.h>
#include <markoff/FoldSpec.h>

namespace Markoff {

/// Per-leaf view state the host persists in workspace.json. Three
/// common fields plus an opaque per-view QJsonObject so each widget
/// can round-trip extras without polluting the base contract.
struct EphemeralState {
    float scroll = 0.0f;
    CursorPos cursor;
    QString viewMode;               // host-defined string; Markoff doesn't interpret
    QVector<FoldSpec> foldedHeadings;
    QJsonObject extras;             // opaque; each view owns its own shape

    QJsonObject toJson() const;
    static EphemeralState fromJson(const QJsonObject &);
};

}  // namespace Markoff
