// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>

#include <crdt/Anchor.h>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// Serialize a CollabText Anchor to JSON. Round-trips losslessly for all
/// anchor values including the min/max sentinels.
MARKOFF_FOUNDATION_EXPORT
QJsonObject anchorToJson(const CollabText::Crdt::Anchor &);

/// Deserialize a CollabText Anchor from JSON.
MARKOFF_FOUNDATION_EXPORT
CollabText::Crdt::Anchor anchorFromJson(const QJsonObject &);

}  // namespace Markoff
