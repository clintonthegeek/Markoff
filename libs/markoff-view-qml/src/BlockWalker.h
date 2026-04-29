// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include <markoff/view/qml/BlockRecord.h>

namespace Markoff::View::Qml {

/// Walks a markdown source string and returns a list of top-level block
/// records. Pure function; no Qt-app or QML dependencies. The classification
/// is intentionally simple (line-based + fence-state); edge cases that
/// require full tree-sitter analysis are deferred to a later phase.
class BlockWalker {
public:
    static QList<BlockRecord> walk(const QString &source);
};

}  // namespace Markoff::View::Qml
