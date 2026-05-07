// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>

#include <crdt/Anchor.h>

#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

/// Serialize a CollabText Anchor to JSON. Round-trips losslessly for all
/// anchor values including the min/max sentinels.
MARKOFF_CORE_EXPORT
QJsonObject anchorToJson(const CollabText::Crdt::Anchor &);

/// Deserialize a CollabText Anchor from JSON.
MARKOFF_CORE_EXPORT
CollabText::Crdt::Anchor anchorFromJson(const QJsonObject &);

}  // namespace Markoff
