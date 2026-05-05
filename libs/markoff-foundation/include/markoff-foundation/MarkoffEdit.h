// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QtGlobal>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// A surgical edit in OLD-text byte coordinates (UTF-8). Wrapper over
/// CollabText::Crdt::TextEdit for foundation-side use; carries the
/// minimum needed for views to apply the change to their display
/// representation.
///
/// oldStart and oldEnd are UTF-8 byte offsets. oldEnd >= oldStart.
/// newText is UTF-8 bytes (empty for pure deletion).
///
/// @deprecated D2: replaced by BlockEdit + StructuralOp. D4 will delete.
struct [[deprecated("D2: replaced by BlockEdit + StructuralOp; D4 will delete")]] MARKOFF_FOUNDATION_EXPORT MarkoffEdit {
    quint32    oldStart = 0;
    quint32    oldEnd = 0;
    QByteArray newText;

    bool isInsertion() const { return oldStart == oldEnd && !newText.isEmpty(); }
    bool isDeletion() const { return oldStart != oldEnd && newText.isEmpty(); }
    bool isReplacement() const { return oldStart != oldEnd && !newText.isEmpty(); }

    QJsonObject toJson() const;
    static MarkoffEdit fromJson(const QJsonObject &);
};

}  // namespace Markoff
