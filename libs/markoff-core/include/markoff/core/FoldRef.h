// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <crdt/Anchor.h>

#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

/// An anchor-bound fold reference. Survives concurrent CRDT edits and
/// arbitrary parse drift. Replaces the lossy (line, level) FoldSpec
/// from the existing Markoff family (audit §3.6).
struct MARKOFF_CORE_EXPORT FoldRef {
    enum class Kind {
        Heading,    ///< fold heading + everything until next heading at <= level
        Block,      ///< fold a specific block range
    };

    Kind                      kind = Kind::Heading;
    CollabText::Crdt::Anchor  start;        ///< anchor at the fold start
    QStringList               headingPath;  ///< for Heading kind
    int                       headingLevel = 0;

    QJsonObject toJson() const;
    static FoldRef fromJson(const QJsonObject &);
};

}  // namespace Markoff
