// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/Selection.h>

#include <QByteArray>
#include <optional>

/// One remote caret or selection. This record is not an accepted document
/// action: losing it loses the caret and leaves history unchanged.
/// The only writer version is MPR/1.
struct EphemeralPresence {
    QString participantId;
    QString participantLabel;
    QColor presenceColor;
    quint64 cursorVersion = 0;
    Markoff::BlockId block;
    QByteArray anchorToken;
    QByteArray activeToken;
};

[[nodiscard]] std::optional<QByteArray> encodeEphemeralPresence(
    const EphemeralPresence& presence);

[[nodiscard]] std::optional<EphemeralPresence> decodeEphemeralPresence(
    const QByteArray& bytes);

[[nodiscard]] std::optional<EphemeralPresence> ephemeralPresenceFromSelection(
    const Markoff::Selection& selection);

[[nodiscard]] Markoff::Selection selectionFromEphemeralPresence(
    const EphemeralPresence& presence);
