// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <optional>

#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/BlockId.h>

namespace Markoff {

/// A portable position within one provider-owned text stream. The token is
/// opaque; BlockId supplies the scope required to resolve it.
struct MARKOFF_CORE_EXPORT ScopedTextPosition {
    BlockId block;
    QByteArray boundaryToken;

    bool isValid() const noexcept
    { return !block.isNull() && !boundaryToken.isEmpty(); }
    bool operator==(const ScopedTextPosition &) const noexcept = default;
};

/// A kinded selection between two scoped positions. Foundation tracks zero or
/// more on each Session: one Primary, plus any number of Secondary,
/// SearchMatch, or Presence selections.
///
struct MARKOFF_CORE_EXPORT Selection {
    enum class Kind {
        Primary,        ///< editable, the typing one (one per session)
        Secondary,      ///< editable, multi-cursor (commands apply to all)
        SearchMatch,    ///< not editable, search controller manages
        Presence,       ///< not editable, remote-session indicator
    };

    std::optional<ScopedTextPosition> anchorPosition;
    std::optional<ScopedTextPosition> activePosition;
    Kind       kind = Kind::Primary;

    // Only meaningful for Kind::Presence
    QString participantId;
    QString participantLabel;
    QColor  presenceColor;
    quint64 cursorVersion = 0;

    bool isEmpty() const;
    QJsonObject toJson() const;
    static Selection fromJson(const QJsonObject &);
};

}  // namespace Markoff
