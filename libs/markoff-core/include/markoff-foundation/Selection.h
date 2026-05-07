// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <markoff-foundation/MarkoffFoundationExport.h>
#include <markoff-foundation/TextAnchor.h>

namespace Markoff {

/// A kinded selection between two anchors. Foundation tracks zero or
/// more on each Session: one Primary, plus any number of Secondary,
/// SearchMatch, or Presence selections.
///
/// `anchor` and `active` are TextAnchors — opaque view-layer-safe
/// wrappers around CRDT byte anchors. They survive concurrent edits
/// (the underlying CRDT identity is preserved); translation back to
/// byte offsets goes through MarkoffDocument::resolveTextAnchor.
struct MARKOFF_FOUNDATION_EXPORT Selection {
    enum class Kind {
        Primary,        ///< editable, the typing one (one per session)
        Secondary,      ///< editable, multi-cursor (commands apply to all)
        SearchMatch,    ///< not editable, search controller manages
        Presence,       ///< not editable, remote-session indicator
    };

    TextAnchor anchor;
    TextAnchor active;
    Kind       kind = Kind::Primary;

    // Only meaningful for Kind::Presence
    QString participantId;
    QString participantLabel;
    QColor  presenceColor;
    quint64 cursorVersion = 0;

    bool isEmpty() const;
    bool isReversed() const;

    QJsonObject toJson() const;
    static Selection fromJson(const QJsonObject &);
};

}  // namespace Markoff
