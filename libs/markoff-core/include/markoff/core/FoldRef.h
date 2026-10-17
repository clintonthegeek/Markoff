// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/Selection.h>

namespace Markoff {

/// A scoped-position fold reference. Survives concurrent edits and
/// arbitrary parse drift. Replaces the lossy (line, level) FoldSpec
/// from the existing Markoff family (audit §3.6).
struct MARKOFF_CORE_EXPORT FoldRef {
    enum class Kind {
        Heading,    ///< fold heading + everything until next heading at <= level
        Block,      ///< fold a specific block range
    };

    Kind                      kind = Kind::Heading;
    std::optional<ScopedTextPosition> startPosition;
    QStringList               headingPath;  ///< for Heading kind
    int                       headingLevel = 0;

    QJsonObject toJson() const;
    static FoldRef fromJson(const QJsonObject &);
};

}  // namespace Markoff
